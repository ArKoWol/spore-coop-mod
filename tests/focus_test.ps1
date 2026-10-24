# Does the game keep simulating while its window is not in the foreground?
# Precondition: the game runs (launch_spore_mod.bat) and is IN the creature stage (a world with animals around).
# It compares two entity dumps taken 10 s apart while another window has the focus, and reads the frame rate the mod logged.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\focus_test.ps1
param([int]$Seconds = 10)

$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'test\SPORE_A\SporebinEP1'
$console = Join-Path $root 'scripts\console.ps1'

function Send($commands) {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $console @commands -Wait 3 2>&1 | Out-String
    return $out
}
function ReadDump {
    $p = Join-Path $dir 'CoopDump.txt'
    if (-not (Test-Path $p)) { return @{} }
    $map = @{}
    foreach ($line in Get-Content $p | Select-Object -Skip 2) {
        if ($line -match '^(\w+) (\w+) (\S+) \| (\S+) (\S+) (\S+)') { $map["$($Matches[1])/$($Matches[2])"] = "$($Matches[4]) $($Matches[5]) $($Matches[6])" }
    }
    return $map
}

if (-not (Get-Process SporeApp -ErrorAction SilentlyContinue)) { Write-Host 'The game is not running.' -ForegroundColor Red; exit 1 }

Write-Host 'dump 1 (game window may be focused)...'
Send @('coop fps', 'coop dump') | Out-Null
$before = ReadDump
if ($before.Count -lt 5) { Write-Host "Only $($before.Count) entities with a position: is the game in the creature stage?" -ForegroundColor Red; exit 1 }

$logStart = @(Get-Content (Join-Path $dir 'CoopMod.log')).Count
Write-Host "taking the focus away from the game for $Seconds s (a Notepad window opens)..."
$np = Start-Process notepad -PassThru
Start-Sleep -Seconds $Seconds
Send @('coop dump') | Out-Null
$after = ReadDump
Stop-Process -Id $np.Id -Force -ErrorAction SilentlyContinue

$moved = 0; $same = 0
foreach ($k in $before.Keys) { if ($after.ContainsKey($k)) { if ($after[$k] -ne $before[$k]) { $moved++ } else { $same++ } } }
$fps = @(Get-Content (Join-Path $dir 'CoopMod.log') | Select-Object -Skip $logStart | Where-Object { $_ -match '^fps: (\d+) \(game window NOT focused\)' } | ForEach-Object { [int]$Matches[1] })

Write-Host ''
Write-Host "entities with a position: $($before.Count); moved while unfocused: $moved; unchanged: $same"
if ($fps.Count) { Write-Host "frames per second while unfocused: min $(($fps | Measure-Object -Minimum).Minimum), max $(($fps | Measure-Object -Maximum).Maximum)" }
else { Write-Host 'no "NOT focused" fps lines in the log (the window kept the focus?)' -ForegroundColor Yellow }
if ($moved -gt 0) { Write-Host 'RESULT: PASS - the world keeps simulating without the focus' -ForegroundColor Green }
else { Write-Host 'RESULT: FAIL - nothing moved while the game was unfocused (the simulation pauses or the game is throttled)' -ForegroundColor Red }
