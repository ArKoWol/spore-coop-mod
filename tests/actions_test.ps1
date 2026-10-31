# Do abilities, effects and animations of our creature reach the other side's copy? One PC, no friend needed.
# Precondition: the game runs (launch_spore_mod.bat) and is IN the creature stage; your creature stands still.
# It sends `coop selftest` (game/SelfTest.cpp: echo puppet on, abilities / an animation / an effect played on our avatar and
# checked on the echo puppet, then keys 1-9 pressed as a player) and prints the selftest lines of CoopMod.log.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\actions_test.ps1
param([int]$TimeoutSeconds = 120)

$root = Split-Path $PSScriptRoot -Parent
$log = Join-Path $root 'test\SPORE_A\SporebinEP1\CoopMod.log'
$console = Join-Path $root 'scripts\console.ps1'

if (-not (Get-Process SporeApp -ErrorAction SilentlyContinue)) { Write-Host 'The game is not running.' -ForegroundColor Red; exit 1 }
if (-not (Test-Path $log)) { Write-Host "No log at $log" -ForegroundColor Red; exit 1 }

$start = (Get-Item $log).Length
& powershell -NoProfile -ExecutionPolicy Bypass -File $console 'coop selftest' -Wait 1 | Out-Null

$until = (Get-Date).AddSeconds($TimeoutSeconds)
$shown = 0
$result = $null
while ((Get-Date) -lt $until -and -not $result) {
    Start-Sleep -Seconds 2
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try {
        if ($stream.Length -lt $start) { $start = 0 }  # the log was rotated
        $stream.Seek($start, 'Begin') | Out-Null
        $text = (New-Object IO.StreamReader($stream)).ReadToEnd()
    } finally { $stream.Close() }
    $lines = @($text -split "`r?`n" | Where-Object { $_ -match 'selftest: |action: (replay|mode action|  word)' })
    for ($i = $shown; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $color = if ($line -match 'PASS') { 'Green' } elseif ($line -match 'FAIL') { 'Red' } else { 'Gray' }
        Write-Host $line -ForegroundColor $color
        if ($line -match 'selftest: RESULT: (PASS|FAIL)') { $result = $Matches[1] }
    }
    $shown = $lines.Count
}

if (-not $result) { Write-Host "No RESULT within $TimeoutSeconds s: is the game in the creature stage, with the mod's console on?" -ForegroundColor Red; exit 1 }
Write-Host "RESULT: $result" -ForegroundColor $(if ($result -eq 'PASS') { 'Green' } else { 'Red' })
exit $(if ($result -eq 'PASS') { 0 } else { 1 })
