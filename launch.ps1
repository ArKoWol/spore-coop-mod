# One-step test launcher: (build, or update from the latest release), start the game windowed and open a PowerShell console
# connected to it. The console window closes by itself when the game exits.
# Usually started through launch_spore_mod.bat.
#
#   .\launch.ps1                       dev: build, install, start, open the console (no git pull: run git pull yourself)
#   .\launch.ps1 -Mode release         install the latest GitHub release (what players get) instead of building
#   .\launch.ps1 -Mode play            start whatever is installed
#   -NoConsole does not open the console window.
param(
    [ValidateSet('dev', 'release', 'play')][string]$Mode = 'dev',
    [switch]$NoConsole
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$game = Join-Path $root 'test\SPORE_A'
$gameDir = Join-Path $game 'SporebinEP1'
$exe = Join-Path $gameDir 'SporeApp.exe'
$log = Join-Path $gameDir 'CoopMod.log'

function Say($text) { Write-Host "[launch] $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "[launch] $text" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }

if (-not (Test-Path $exe)) { Fail 'test\SPORE_A is missing. Run setup.ps1 -GameDir "<your Spore folder>" first.' }
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
Add-Content $log "===== $(Get-Date -Format 'dd.MM.yyyy HH:mm:ss') launch ($Mode) =====" -Encoding ascii
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

# -- the console window, connected to the game; it closes when the game exits
if (-not $NoConsole) {
    Start-Process powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$root\console.ps1`"")
    Say 'console window opened (it closes with the game). Type: coop host | coopjoin <steamid> | coop status | exit'
}

