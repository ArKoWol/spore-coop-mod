# Builds coop_package\ and coop_package.zip from the loader download and the staged CoopMod.dll.
# Valve's steam_api.dll is NOT included (update.ps1 finds one in the player's Steam games);
# pass -IncludeSteamDll for a private hand-over.
param([switch]$IncludeSteamDll, [string]$Repo)

$root = $PSScriptRoot
$loader = (Get-ChildItem (Join-Path $root 'downloads') -Directory -Filter 'SporeModLoader-*' | Sort-Object Name | Select-Object -Last 1).FullName
$dll = Join-Path $root 'mod\CoopMod\staging\CoopMod.dll'
$out = Join-Path $root 'coop_package'

if (-not (Test-Path $dll)) { throw "CoopMod.dll not found: $dll" }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }

New-Item -ItemType Directory -Force "$out\SporebinEP1", "$out\SporeModLoader\CoreLibs", "$out\SporeModLoader\ModLibs", "$out\coop_update" | Out-Null
Copy-Item "$loader\SporebinEP1\dinput8.dll" "$out\SporebinEP1\"
Copy-Item "$loader\SporeModLoader\CoreLibs\*" "$out\SporeModLoader\CoreLibs\"
Copy-Item $dll "$out\SporeModLoader\ModLibs\CoopMod.dll"
if ($IncludeSteamDll) {
    Copy-Item (Join-Path $root 'downloads\steam_api\steam_api_coop.dll') "$out\SporebinEP1\steam_api_coop.dll"
}
Copy-Item (Join-Path $root 'coop_package_README.txt') "$out\README.txt"
Copy-Item (Join-Path $root 'local\fix_spore_registry.ps1') "$out\fix_spore_registry.ps1"
Copy-Item (Join-Path $root 'update\update.ps1'), (Join-Path $root 'update\update.bat') "$out\coop_update\"
if ($Repo) { Set-Content "$out\coop_update\repo.txt" $Repo -Encoding ascii }

$zip = Join-Path $root 'coop_package.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$out\*" -DestinationPath $zip
Get-ChildItem $out -Recurse -File | Select-Object @{n='file';e={$_.FullName.Substring($out.Length + 1)}}, Length
"$zip  $((Get-Item $zip).Length) bytes  sha256 $((Get-FileHash $zip).Hash)"

