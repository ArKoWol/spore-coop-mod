# Builds mod\CoopMod\staging\CoopMod.package (a Spore DBPF package) from the files in assets\.
# Called by build.bat; the launcher, make_package.ps1 and update.ps1 install it as <game>\Data\CoopMod.package.
#   assets\splash.png  ->  coopmod!splash.png  (the picture shown after the EA logo, 16:9, see CHANGELOG)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out = Join-Path $PSScriptRoot 'staging\CoopMod.package'

# Spore resource names are FNV-1 hashes of the lower-case name (ModAPI's id("...")).
function Get-SporeId([string]$name) {
    [uint64]$h = 2166136261  # 0x811C9DC5 (hex literals above 0x7FFFFFFF are negative in PowerShell 5.1)
    foreach ($b in [Text.Encoding]::ASCII.GetBytes($name.ToLowerInvariant())) {
        $h = ($h * 16777619) % 4294967296  # FNV prime 0x01000193
        $h = $h -bxor $b
    }
    return [uint32]$h
}

$png = 0x2F7D0004
$resources = @(
    @{ Type = $png; Group = (Get-SporeId 'coopmod'); Instance = (Get-SporeId 'splash'); File = Join-Path $root 'assets\splash.png' }
)

New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
$stream = [IO.File]::Create($out)
$w = New-Object IO.BinaryWriter($stream)
try {
    $w.Write((New-Object byte[] 96))  # header, written at the end
    $entries = @()
    foreach ($r in $resources) {
        $data = [IO.File]::ReadAllBytes($r.File)
        $entries += @{ R = $r; Offset = [uint32]$stream.Position; Size = [uint32]$data.Length }
        $w.Write($data)
    }
    # index: flags 4 = the third key field is the same for all entries (0); entries are uncompressed
    $indexOffset = [uint32]$stream.Position
    $w.Write([uint32]4); $w.Write([uint32]0)
    foreach ($e in $entries) {
        $w.Write([uint32]$e.R.Type); $w.Write([uint32]$e.R.Group); $w.Write([uint32]$e.R.Instance)
        $w.Write($e.Offset); $w.Write([uint32]($e.Size + 2147483648)); $w.Write($e.Size)
        $w.Write([uint16]0); $w.Write([uint16]1)
    }
    $indexSize = [uint32]($stream.Position - $indexOffset)
    $stream.Position = 0
    $w.Write([Text.Encoding]::ASCII.GetBytes('DBPF')); $w.Write([uint32]3); $w.Write([uint32]0)
    $stream.Position = 0x24; $w.Write([uint32]$entries.Count)
    $stream.Position = 0x2C; $w.Write($indexSize)
    $stream.Position = 0x3C; $w.Write([uint32]3)
    $stream.Position = 0x40; $w.Write($indexOffset)
}
finally {
    $w.Close()
}
Write-Host "CoopMod.package: $($entries.Count) resource(s)"
