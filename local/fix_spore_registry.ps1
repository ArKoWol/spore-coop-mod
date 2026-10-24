# Writes the registry values the GOG installer should have written, if Spore shows
# "Configuration Script Failed [2000]" / "data directory is missing or corrupt [1004]" on start.
# Run this in PowerShell as Administrator:  .\fix_spore_registry.ps1 -GameDir "D:\Games\SPORE"
param([Parameter(Mandatory = $true)][string]$GameDir)

$GameDir = (Resolve-Path $GameDir).Path.TrimEnd('\')
if (-not (Test-Path "$GameDir\SporebinEP1\SporeApp.exe")) { throw "SporebinEP1\SporeApp.exe not found in $GameDir" }

function Set-Val($key, $name, $value, $type) {
    if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    New-ItemProperty -Path $key -Name $name -Value $value -PropertyType $type -Force | Out-Null
}

$base = 'HKLM:\SOFTWARE\WOW6432Node\Electronic Arts'
Set-Val "$base\spore" 'reindexyesorno' 0 DWord
Set-Val "$base\spore" 'installcompleted' 1 DWord
Set-Val "$base\spore" 'appdir' 'Spore' String
Set-Val "$base\spore" 'installloc' $GameDir String
Set-Val "$base\spore" 'datadir' "$GameDir\Data" String
Set-Val "$base\spore" 'playerdir' 'My Spore Creations' String
Set-Val "$base\spore" 'locale' 'en-us' String
Set-Val "$base\spore_EP1" 'AddOnID' 2 DWord
Set-Val "$base\spore_EP1" 'PackID' 128448390 DWord
Set-Val "$base\spore_EP1" 'DataDir' "$GameDir\DataEP1\" String
Set-Val "$base\spore Creepy and Cute Parts Pack" 'AddOnID' 1 DWord
Set-Val "$base\spore Creepy and Cute Parts Pack" 'PackID' 116700625 DWord
Set-Val "$base\spore Creepy and Cute Parts Pack" 'DataDir' "$GameDir\bp1content\" String
"Registry updated for $GameDir"
