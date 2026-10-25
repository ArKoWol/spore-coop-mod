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

if (-not (Test-Path $exe)) { Fail 'test\SPORE_A is missing. Run scripts\setup.ps1 -GameDir "<your Spore folder>" first.' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { Write-Host '[launch] Steam is not running: co-op over Steam will not work until you start it.' -ForegroundColor Yellow }

# -- close a running game the normal way (it saves), kill it only if it does not react
$running = Get-Process SporeApp -ErrorAction SilentlyContinue
if ($running) {
    Say 'closing the running game...'
    $running | ForEach-Object { [void]$_.CloseMainWindow() }
    try { $running | Wait-Process -Timeout 15 -ErrorAction Stop }
    catch { Say 'it did not close, killing it'; $running | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep 2 }
}

# -- get the mod
switch ($Mode) {
    'dev' {
        if (-not $NoUpdateCheck) {
            # looks at GitHub for new commits of the repo and asks whether to pull them (silent when there is nothing new)
            try { & (Join-Path $root 'package\coop_update\update.ps1') -Check -Only repo } catch { Write-Host "[launch] update check failed: $($_.Exception.Message)" -ForegroundColor Yellow }
        }
        Say 'building the mod...'
        & cmd /c "`"$root\mod\CoopMod\build.bat`" > `"$env:TEMP\coopmod_build.log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { Get-Content "$env:TEMP\coopmod_build.log" -Tail 25; Fail 'the build failed, the game was not started.' }
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
Say 'game started, waiting for the mod...'

# wait for the mod's "initialized" line in its log (connecting to its console port here would kick the console window out:
# the port serves one client at a time)
$ready = $false
for ($i = 0; $i -lt 120 -and -not $ready; $i++) {
    Start-Sleep -Milliseconds 750
    if (-not (Get-Process SporeApp -ErrorAction SilentlyContinue)) { Fail 'the game closed during start (see CoopMod.log and the Windows Application log).' }
    if (Test-Path $log) {
        $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        try { $null = $fs.Seek($logStart, 'Begin'); $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Close() }
        if ($text -match 'CoopMod initialized') { $ready = $true }
    }
}
if (-not $ready) { Fail 'the mod did not start in 90 s. Is CoopMod.dll installed (CoopMod.log)?' }
if ($NoConsole) { Say 'started without the console window (use console.bat to connect).' }
else { Say 'the mod opened the console window (it closes with the game). Type: coop host | coopjoin <steamid> | coop status | exit' }