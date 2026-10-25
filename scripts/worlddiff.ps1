# Compares two CoopDump.txt files (made by `coop dump` on two machines in the same save): gate G0, docs/PLAN.md section 10.
#   .\scripts\worlddiff.ps1 host\CoopDump.txt guest\CoopDump.txt
# Prints the count per kind of object on both sides, the objects that exist only on one side (same kind, rounded
# position 0.5 m) and the creature species the guest cannot build.
param(
    [Parameter(Mandatory)][string]$Host_,
    [Parameter(Mandatory)][string]$Guest,
    [double]$Cell = 0.5,
    [int]$ShowMax = 12
)

function Read-Dump([string]$path) {
    $objs = @(); $species = @()
    foreach ($line in Get-Content $path) {
        if ($line -match '^species ') { $species += $line; continue }
        if ($line -notmatch '^[0-9a-f]{8} [0-9a-f]{8} (\S+)') { continue }
        $name = $Matches[1]
        $pos = if ($line -match '\| (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)') { "{0:F0},{1:F0},{2:F0}" -f ([math]::Round([double]$Matches[1] / $Cell)), ([math]::Round([double]$Matches[2] / $Cell)), ([math]::Round([double]$Matches[3] / $Cell)) } else { 'nopos' }
        $objs += [pscustomobject]@{ Name = $name; Key = "$name@$pos"; Line = $line }
    }
    [pscustomobject]@{ Objects = $objs; Species = $species }
}

$h = Read-Dump $Host_; $g = Read-Dump $Guest
Write-Host "== counts per kind (host / guest)"
$names = @($h.Objects.Name) + @($g.Objects.Name) | Sort-Object -Unique
foreach ($n in $names) {
    $a = @($h.Objects | ? Name -eq $n).Count; $b = @($g.Objects | ? Name -eq $n).Count
    $mark = if ($a -eq $b) { 'ok  ' } else { 'DIFF' }
    "{0} {1,-28} {2,5} / {3,5}" -f $mark, $n, $a, $b
}
$hk = @{}; $h.Objects | % { $hk[$_.Key] = 1 + [int]$hk[$_.Key] }
$gk = @{}; $g.Objects | % { $gk[$_.Key] = 1 + [int]$gk[$_.Key] }
foreach ($side in @(@('host', $hk, $gk), @('guest', $gk, $hk))) {
    $only = @($side[1].Keys | ? { -not $side[2].ContainsKey($_) } | Sort-Object)
    Write-Host "== only on the $($side[0]): $($only.Count)"
    $only | Select-Object -First $ShowMax | % { "   $_" }
}
Write-Host "== species lines (guest)"
$g.Species | Select-Object -First 40 | % { "   $_" }
$bad = @($g.Species | ? { $_ -match 'buildable=0' }).Count
Write-Host "guest cannot build $bad species"
