# One-PC co-op test: starts two copies of the game side by side, A (test\SPORE_A) and B (test\SPORE_B), each windowed with its
# own console window. Then in A's console: coop localhost, in B's: coop localjoin (or start with -Connect).
# Usually started through launch_two.bat. How it works: docs/TESTING.md ("Two games on one PC").
#
#   .\launch_two.ps1                 build, install, start A and B
#   .\launch_two.ps1 -Mode play      start what is installed
#   -Connect     the consoles type the commands themselves (A hosts, B joins; B also joins again after each restart)
#   -ResetB      B gets a fresh copy of A's save data (B's world, creature and caches are deleted first)
#
# test\SPORE_B is made once from test\SPORE_A without copying the game: SporebinEP1 holds hard links of A's files (its own
# logs and CoopModels), SporeModLoader its own log with junctions to A's CoreLibs and ModLibs (one CoopMod.dll for both),
# everything else is a junction. B's %APPDATA% is test\SPORE_B\CoopAppData (the mod redirects the game there, game/Instance.cpp).
param(
    [ValidateSet('dev', 'play')][string]$Mode = 'dev',
    [switch]$Connect,
    [switch]$ResetB
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gameA = Join-Path $root 'test\SPORE_A'
$gameB = Join-Path $root 'test\SPORE_B'
$appB = Join-Path $gameB 'CoopAppData'

function Say($text) { Write-Host "[launch_two] $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "[launch_two] $text" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }

if (-not (Test-Path (Join-Path $gameA 'SporebinEP1\SporeApp.exe'))) { Fail 'test\SPORE_A is missing. Run scripts\setup.ps1 first.' }

# -- make test\SPORE_B (hard links and junctions only, nothing large is copied)
function Link-Dir($link, $target) { if (-not (Test-Path $link)) { [void](New-Item -ItemType Junction -Path $link -Target $target) } }
function Link-File($link, $target) { if (-not (Test-Path $link)) { [void](New-Item -ItemType HardLink -Path $link -Target $target) } }
$isOwn = { param($name) $name -like '*.log' -or $name -like 'Exception Report*' -or $name -eq 'CoopDump.txt' -or $name -eq 'spore_log.txt' }
if (-not (Test-Path $gameB)) { Say 'making test\SPORE_B (hard links and junctions to test\SPORE_A)...' }
[void](New-Item -ItemType Directory -Force $gameB)
$realA = (Get-Item $gameA).Target  # the junction's target; hard links need the real path on the same volume
if ($realA -is [array]) { $realA = $realA[0] }
if (-not $realA) { $realA = $gameA }
foreach ($item in Get-ChildItem $realA -Force) {
    $link = Join-Path $gameB $item.Name
    if ($item.Name -eq 'SporebinEP1') {
        [void](New-Item -ItemType Directory -Force $link)
        [void](New-Item -ItemType Directory -Force (Join-Path $link 'CoopModels'))
        foreach ($f in Get-ChildItem $item.FullName -File -Force) {
            if (-not (& $isOwn $f.Name)) { Link-File (Join-Path $link $f.Name) $f.FullName }
        }
    }
    elseif ($item.Name -eq 'SporeModLoader') {
        [void](New-Item -ItemType Directory -Force $link)
        Link-Dir (Join-Path $link 'CoreLibs') (Join-Path $item.FullName 'CoreLibs')
        Link-Dir (Join-Path $link 'ModLibs') (Join-Path $item.FullName 'ModLibs')
    }
    elseif ($item.PSIsContainer) { Link-Dir $link $item.FullName }
    elseif (-not (& $isOwn $item.Name)) { Link-File $link $item.FullName }
}
# a new exe or file in A (an update) must reach B: a hard link replaced in A (Copy-Item writes a new file) is re-linked here
foreach ($f in Get-ChildItem (Join-Path $realA 'SporebinEP1') -File -Force) {
    if (& $isOwn $f.Name) { continue }
    $b = Join-Path $gameB "SporebinEP1\$($f.Name)"
    if ((Test-Path $b) -and ((Get-Item $b).Length -ne $f.Length -or (Get-Item $b).LastWriteTime -ne $f.LastWriteTime)) {
        Remove-Item $b -Force; Link-File $b $f.FullName
    }
}

# -- B's own %APPDATA%\Spore: a copy of A's small files and save, no caches (the game makes its own GraphicsCache)
$sporeA = Join-Path $env:APPDATA 'Spore'
$sporeB = Join-Path $appB 'Spore'
if ($ResetB -and (Test-Path $sporeB)) {
    if (Get-Process SporeApp -ErrorAction SilentlyContinue) { Fail 'close the games before -ResetB.' }
    Say "deleting B's data ($sporeB)"
    Remove-Item $sporeB -Recurse -Force
}
if (-not (Test-Path $sporeB)) {
    Say "making B's APPDATA from yours (save, creations, preferences; about 5 MB)"
    [void](New-Item -ItemType Directory -Force (Join-Path $sporeB 'Games'))
    foreach ($name in 'Preferences', 'EditorSaves.package', 'Planets.package', 'Pollination.package', 'RigblockInfo.package') {
        $from = Join-Path $sporeA $name
        if (Test-Path $from) { Copy-Item $from (Join-Path $sporeB $name) -Recurse -Force }
    }
    foreach ($name in 'Game0', 'CoopOwnCreature.txt', 'CoopMigrateAsked.txt') {
        $from = Join-Path $sporeA "Games\$name"
        if (Test-Path $from) { Copy-Item $from (Join-Path $sporeB "Games\$name") -Recurse -Force }
    }
}

# -- A: the normal launcher (closes running games, builds, installs into A, which B shares, starts A and waits for the mod)
$env:COOP_MULTI = '1'  # both copies: whichever starts second (also after an automatic restart) passes the mutex
if ($Connect) { $env:COOP_CONSOLE_RUN = 'coop localhost' } else { Remove-Item Env:COOP_CONSOLE_RUN -ErrorAction SilentlyContinue }
& (Join-Path $PSScriptRoot 'launch.ps1') -Mode $Mode -NoUpdateCheck
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# -- B: its own APPDATA, console port, local port, no Steam (a second copy of the same Steam user)
$dirB = Join-Path $gameB 'SporebinEP1'
$logB = Join-Path $dirB 'CoopMod.log'
$env:COOP_APPDATA = $appB
$env:APPDATA = $appB
$env:COOP_CONSOLE_PORT = '27781'
$env:COOP_LOCAL_PORT = '27791'
$env:COOP_NO_STEAM = '1'
# its own %TEMP%: the mod's UI pictures (CoopModUI\ui_N.package) and the restart helper live there and would clash with A's
$env:TMP = $env:TEMP = Join-Path $appB 'Temp'
[void](New-Item -ItemType Directory -Force $env:TEMP)
$env:COOP_CONSOLE_PS1 = Join-Path $PSScriptRoot 'console.ps1'
if ($Connect) { $env:COOP_CONSOLE_RUN = 'coop localjoin' }
Add-Content $logB "===== $(Get-Date -Format 'dd.MM.yyyy HH:mm:ss') launch_two (B) =====" -Encoding ascii
$start = (Get-Item $logB).Length
$b = Start-Process (Join-Path $dirB 'SporeApp.exe') -ArgumentList '-w' -WorkingDirectory $dirB -PassThru
Say 'B started, waiting for the mod...'
$deadline = (Get-Date).AddSeconds(120)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 750
    if ($b.HasExited) { Fail "B closed during start (exit code $($b.ExitCode)); see $logB" }
    $fs = [IO.File]::Open($logB, 'Open', 'Read', 'ReadWrite')
    try { $null = $fs.Seek($start, 'Begin'); $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Close() }
    if ($text -match 'CoopMod initialized') { break }
}
if ($text -notmatch 'CoopMod initialized') { Fail "B's mod did not start in 120 s (see $logB)." }
Say 'both games run. A: console port 27780, local port 27790. B: console port 27781, local port 27791.'
if (-not $Connect) { Say "In A's console: coop localhost   In B's console: coop localjoin" }
Say "Logs: test\SPORE_A\SporebinEP1\CoopMod.log and test\SPORE_B\SporebinEP1\CoopMod.log"
