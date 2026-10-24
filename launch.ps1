# One-step test launcher: (pull and build or update), start the game windowed, open a PowerShell console connected
# to it, optionally host or join. Usually started through launch_spore_mod.bat / host.bat / join.bat.
#
#   .\launch.ps1                       dev: git pull, build, install, start, open the console
#   .\launch.ps1 -AsHost               ... and run "coop host"; your Steam ID goes to the clipboard
#   .\launch.ps1 -JoinHost 7656...     ... and run "coopjoin <id>"; "-JoinHost last" reuses the previous id
#   .\launch.ps1 -Mode release         install the latest GitHub release (what players get) instead of building
#   .\launch.ps1 -Mode play            start whatever is installed
#   -NoPull skips git pull in dev mode, -NoConsole does not open the console window.
param(
    [ValidateSet('dev', 'release', 'play')][string]$Mode = 'dev',
    [switch]$AsHost,
    [string]$JoinHost,
    [switch]$NoPull,
    [switch]$NoConsole
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$game = Join-Path $root 'test\SPORE_A'
$gameDir = Join-Path $game 'SporebinEP1'
$exe = Join-Path $gameDir 'SporeApp.exe'
$log = Join-Path $gameDir 'CoopMod.log'
$lastHostFile = Join-Path $root 'local\last_host.txt'

function Say($text) { Write-Host "[launch] $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "[launch] $text" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }

if (-not (Test-Path $exe)) { Fail 'test\SPORE_A is missing. Run setup.ps1 -GameDir "<your Spore folder>" first.' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { Write-Host '[launch] Steam is not running: co-op over Steam will not work until you start it.' -ForegroundColor Yellow }

# -- the host id to join, decided before anything starts so a prompt does not wait behind the game
$commands = @()
if ($AsHost) { $commands += 'coop host' }
if ($PSBoundParameters.ContainsKey('JoinHost')) {
    $id = $JoinHost.Trim()
    if (-not $id -or $id -eq 'last') {
        $id = if (Test-Path $lastHostFile) { (Get-Content $lastHostFile -Raw).Trim() } else { '' }
        if (-not $id) { $id = (Read-Host 'Steam ID of the host (the long number from "coop host")').Trim() }
    }
    if ($id -notmatch '^\d{10,20}$') { Fail "'$id' is not a Steam ID (a long number)." }
    New-Item -ItemType Directory -Force (Split-Path $lastHostFile) | Out-Null
    Set-Content $lastHostFile $id -Encoding ascii
    $commands += "coopjoin $id"
}

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
        if (-not $NoPull -and (Test-Path (Join-Path $root '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
            Say 'git pull...'
            $out = git -C $root pull --ff-only 2>&1 | Out-String
            if ($LASTEXITCODE -ne 0) { Write-Host "[launch] git pull did not work (continuing with what you have):`n$out" -ForegroundColor Yellow }
            else { Write-Host $out.Trim() }
        }
        Say 'building the mod...'
        & cmd /c "`"$root\mod\CoopMod\build.bat`" > `"$env:TEMP\coopmod_build.log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { Get-Content "$env:TEMP\coopmod_build.log" -Tail 25; Fail 'the build failed, the game was not started.' }
        Copy-Item (Join-Path $root 'mod\CoopMod\staging\CoopMod.dll') (Join-Path $game 'SporeModLoader\ModLibs\CoopMod.dll') -Force
        Say 'installed the local build.'
    }
    'release' {
        Say 'installing the latest release...'
        try { & (Join-Path $root 'update\update.ps1') -GameDir $game } catch { Fail "update failed: $($_.Exception.Message)" }
    }
}

# -- start the game (windowed: a minimized or unfocused fullscreen Spore runs no frames, so no co-op and no console)
$logLines = if (Test-Path $log) { @(Get-Content $log).Count } else { 0 }
Add-Content $log "===== $(Get-Date -Format 'dd.MM.yyyy HH:mm:ss') launch ($Mode) =====" -Encoding ascii
$logLines++
Start-Process $exe -ArgumentList '-w' -WorkingDirectory $gameDir
Say 'game started, waiting for the mod...'

$ready = $false
for ($i = 0; $i -lt 120 -and -not $ready; $i++) {
    Start-Sleep -Milliseconds 750
    if (-not (Get-Process SporeApp -ErrorAction SilentlyContinue)) { Fail 'the game closed during start (see CoopMod.log and the Windows Application log).' }
    $c = New-Object Net.Sockets.TcpClient
    try { $c.Connect('127.0.0.1', 27780); $ready = $true } catch { } finally { $c.Close() }
}
if (-not $ready) { Fail 'the mod console did not come up in 90 s. Is CoopMod.dll installed (CoopMod.log)?' }
Start-Sleep -Milliseconds 700

# -- the console window, connected to the game, with the starting commands already sent
if (-not $NoConsole) {
    $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', "`"$root\console.ps1`"")
    if ($commands) { $args += @('-Run', "`"$($commands -join ';')`"") }
    Start-Process powershell -ArgumentList $args
    Say 'console window opened.'
}

# -- hosting: put the Steam ID on the clipboard as soon as the mod reports it
if ($AsHost) {
    for ($i = 0; $i -lt 40; $i++) {
        $line = @(Get-Content $log | Select-Object -Skip $logLines) | Where-Object { $_ -match 'steam: ready, my Steam ID (\d+)' } | Select-Object -First 1
        if ($line -and $line -match 'my Steam ID (\d+)') {
            Set-Clipboard $Matches[1]
            Say "your Steam ID $($Matches[1]) is on the clipboard: send it to the other player (they run join.bat)."
            break
        }
        Start-Sleep -Milliseconds 500
    }
}
Say 'done. Type console commands in the console window (coop status, coop off, stage creature ...).'
