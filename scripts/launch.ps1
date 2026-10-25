# One-step test launcher: (build, or update from the latest release), start the game windowed and open a PowerShell console
# connected to it. The console window closes by itself when the game exits.
# Usually started through launch_spore_mod.bat.
#
#   .\launch.ps1                       dev: check GitHub for new commits (asks whether to pull them), build, install,
#                                      start, open the console
#   .\launch.ps1 -Mode release         install the latest GitHub release (what players get) instead of building
#   .\launch.ps1 -Mode play            start whatever is installed
#   -NoConsole does not open the console window, -NoUpdateCheck skips the question about new commits.
param(
    [ValidateSet('dev', 'release', 'play')][string]$Mode = 'dev',
    [switch]$NoConsole,
    [switch]$NoUpdateCheck
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent  # the repo (this script lives in scripts\)
$game = Join-Path $root 'test\SPORE_A'
$gameDir = Join-Path $game 'SporebinEP1'
$exe = Join-Path $gameDir 'SporeApp.exe'
$log = Join-Path $gameDir 'CoopMod.log'

function Say($text) { Write-Host "[launch] $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "[launch] $text" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }

# Shows "[launch] | <text>... 12s" with a turning bar until $Done returns $true (or the timeout), so a long step does not
# look like a hung window; then "<text>: done (14 s)". Returns $false on timeout.
function Wait-Spinner([string]$Text, [scriptblock]$Done, [int]$TimeoutSec = 0) {
    $frames = '|', '/', '-', '\'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $i = 0
    $ok = $true
    while (-not (& $Done)) {
        if ($TimeoutSec -and $watch.Elapsed.TotalSeconds -ge $TimeoutSec) { $ok = $false; break }
        Write-Host -NoNewline ("`r[launch] {0} {1}... {2}s   " -f $frames[$i % 4], $Text, [int]$watch.Elapsed.TotalSeconds) -ForegroundColor Cyan
        $i++
        Start-Sleep -Milliseconds 120
    }
    $result = if ($ok) { 'done' } else { 'timed out' }
    Write-Host ("`r[launch] {0}: {1} ({2} s)          " -f $Text, $result, [int]$watch.Elapsed.TotalSeconds) -ForegroundColor Cyan
    return $ok
}

# Runs a program hidden in this window with its output in a log file, with a spinner; returns its exit code.
function Invoke-WithSpinner([string]$Text, [string]$File, [string]$Arguments, [string]$Log) {
    $p = Start-Process $File -ArgumentList $Arguments -NoNewWindow -PassThru -RedirectStandardOutput $Log -RedirectStandardError "$Log.err"
    $null = $p.Handle  # keeps the handle so ExitCode is available after the process ended
    [void](Wait-Spinner $Text { $p.HasExited })
    $p.WaitForExit()
    return $p.ExitCode
}

if (-not (Test-Path $exe)) { Fail 'test\SPORE_A is missing. Run scripts\setup.ps1 -GameDir "<your Spore folder>" first.' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { Write-Host '[launch] Steam is not running: co-op over Steam will not work until you start it.' -ForegroundColor Yellow }

# -- close a running game the normal way (it saves), kill it only if it does not react
$running = Get-Process SporeApp -ErrorAction SilentlyContinue
if ($running) {
    $running | ForEach-Object { [void]$_.CloseMainWindow() }
    $closed = Wait-Spinner 'closing the running game' { -not (Get-Process SporeApp -ErrorAction SilentlyContinue) } -TimeoutSec 15
    if (-not $closed) { Say 'it did not close, killing it'; $running | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep 2 }
}

# -- get the mod
switch ($Mode) {
    'dev' {
        if (-not $NoUpdateCheck) {
            # the slow part (talking to GitHub) with a spinner; then the updater compares and asks whether to pull
            # (silent when there is nothing new; its own fetch is quick now)
            if (Get-Command git -ErrorAction SilentlyContinue) {
                [void](Invoke-WithSpinner 'checking GitHub for new commits' 'git' "-C `"$root`" fetch -q origin" "$env:TEMP\coopmod_fetch.log")
            }
            try { & (Join-Path $root 'package\coop_update\update.ps1') -Check -Only repo } catch { Write-Host "[launch] update check failed: $($_.Exception.Message)" -ForegroundColor Yellow }
        }
        $buildLog = "$env:TEMP\coopmod_build.log"
        $exit = Invoke-WithSpinner 'building the mod' 'cmd.exe' "/c `"`"$root\mod\CoopMod\build.bat`" > `"$buildLog`" 2>&1`"" "$buildLog.out"
        if ($exit -ne 0) { Get-Content $buildLog -Tail 25; Fail 'the build failed, the game was not started.' }
        Copy-Item (Join-Path $root 'mod\CoopMod\staging\CoopMod.dll') (Join-Path $game 'SporeModLoader\ModLibs\CoopMod.dll') -Force
        Copy-Item (Join-Path $root 'mod\CoopMod\staging\CoopMod.package') (Join-Path $game 'Data\CoopMod.package') -Force
        Say 'installed the local build.'
    }
    'release' {
        Say 'installing the latest release...'
        try { & (Join-Path $root 'package\coop_update\update.ps1') -GameDir $game -Install } catch { Fail "update failed: $($_.Exception.Message)" }
    }
}

# -- start the game (windowed: a minimized or unfocused fullscreen Spore runs no frames, so no co-op and no console)
Add-Content $log "===== $(Get-Date -Format 'dd.MM.yyyy HH:mm:ss') launch ($Mode) =====" -Encoding ascii
# the mod opens the console window itself at every start of the game, also after its automatic restart
# (COOP_NO_CONSOLE turns that off, COOP_CONSOLE_PS1 tells it which script)
$env:COOP_CONSOLE_PS1 = Join-Path $PSScriptRoot 'console.ps1'
if ($NoConsole) { $env:COOP_NO_CONSOLE = '1' } else { Remove-Item Env:COOP_NO_CONSOLE -ErrorAction SilentlyContinue }
$logStart = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
Start-Process $exe -ArgumentList '-w' -WorkingDirectory $gameDir

# wait for the mod's "initialized" line in its log (connecting to its console port here would kick the console window out:
# the port serves one client at a time); the log is read every 0.75 s, with a spinner
$script:gameGone = $false
$script:nextTry = [DateTime]::MinValue
$ready = Wait-Spinner 'game started, waiting for the mod' {
    if (-not (Get-Process SporeApp -ErrorAction SilentlyContinue)) { $script:gameGone = $true; return $true }
    if ([DateTime]::Now -lt $script:nextTry) { return $false }
    $script:nextTry = [DateTime]::Now.AddMilliseconds(750)
    if (-not (Test-Path $log)) { return $false }
    $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try { $null = $fs.Seek($logStart, 'Begin'); $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Close() }
    return $text -match 'CoopMod initialized'
} -TimeoutSec 90
if ($script:gameGone) { Fail 'the game closed during start (see CoopMod.log and the Windows Application log).' }
if (-not $ready) { Fail 'the mod did not start in 90 s. Is CoopMod.dll installed (CoopMod.log)?' }
if ($NoConsole) { Say 'started without the console window (use console.bat to connect).' }
else { Say 'the mod opened the console window (it closes with the game). Type: coop host | coopjoin last | coop status | exit' }
