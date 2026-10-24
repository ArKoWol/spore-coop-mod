# Installs or updates the Spore co-op mod from the latest GitHub release.
# Double-click update.bat (it calls this script). Close the game first.
param([string]$GameDir)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$configPath = Join-Path $here 'coop_update.json'
$config = if (Test-Path $configPath) { Get-Content $configPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }

function Save-Config { $config | ConvertTo-Json | Set-Content $configPath -Encoding utf8 }

# --- which GitHub repo
$repoFile = Join-Path $here 'repo.txt'
if (-not $config.repo) {
    if (-not (Test-Path $repoFile)) { throw "repo.txt is missing next to update.ps1" }
    $config | Add-Member -NotePropertyName repo -NotePropertyValue ((Get-Content $repoFile -Raw).Trim()) -Force
}

# --- which game folder
if (-not $GameDir) { $GameDir = $config.gameDir }
if (-not $GameDir -and (Test-Path (Join-Path (Split-Path $here -Parent) 'SporebinEP1\SporeApp.exe'))) { $GameDir = Split-Path $here -Parent }
if (-not $GameDir) { $GameDir = Read-Host 'Path to the Spore game folder (the one that contains SporebinEP1)' }
$GameDir = $GameDir.Trim('"').TrimEnd('\')
if (-not (Test-Path "$GameDir\SporebinEP1\SporeApp.exe")) { throw "SporebinEP1\SporeApp.exe not found in $GameDir" }
$config | Add-Member -NotePropertyName gameDir -NotePropertyValue $GameDir -Force

if (Get-Process SporeApp -ErrorAction SilentlyContinue) { throw 'Spore is running. Close the game and run the update again.' }

# --- latest release
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$headers = @{ 'User-Agent' = 'spore-coop-updater' }
$release = Invoke-RestMethod "https://api.github.com/repos/$($config.repo)/releases/latest" -Headers $headers
$asset = $release.assets | Where-Object name -eq 'coop_package.zip' | Select-Object -First 1
if (-not $asset) { throw "Release $($release.tag_name) has no coop_package.zip" }

if ($config.installed -eq $release.tag_name -and (Test-Path "$GameDir\SporeModLoader\ModLibs\CoopMod.dll")) {
    Write-Host "Already up to date ($($release.tag_name))."
    Save-Config
    return
}

Write-Host "Downloading $($release.tag_name)..."
$tmp = Join-Path $env:TEMP "coop_update_$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $zip = Join-Path $tmp 'coop_package.zip'
    Invoke-WebRequest $asset.browser_download_url -OutFile $zip -Headers $headers
    Expand-Archive $zip (Join-Path $tmp 'pkg') -Force
    $pkg = Join-Path $tmp 'pkg'

    Copy-Item "$pkg\SporebinEP1\dinput8.dll" "$GameDir\SporebinEP1\" -Force
    New-Item -ItemType Directory -Force "$GameDir\SporeModLoader\CoreLibs", "$GameDir\SporeModLoader\ModLibs" | Out-Null
    Copy-Item "$pkg\SporeModLoader\CoreLibs\*" "$GameDir\SporeModLoader\CoreLibs\" -Force
    Copy-Item "$pkg\SporeModLoader\ModLibs\*" "$GameDir\SporeModLoader\ModLibs\" -Force
    if (Test-Path "$pkg\README.txt") { Copy-Item "$pkg\README.txt" "$GameDir\COOP_README.txt" -Force }
    if (Test-Path "$pkg\fix_spore_registry.ps1") { Copy-Item "$pkg\fix_spore_registry.ps1" $here -Force }
}
finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

# --- Steam API: a modern 32-bit steam_api.dll, kept as steam_api_coop.dll (not shipped in the release)
$steamDll = "$GameDir\SporebinEP1\steam_api_coop.dll"
if (-not (Test-Path $steamDll)) {
    $libs = @()
    $steamRoot = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if ($steamRoot) {
        $libs += Join-Path $steamRoot 'steamapps\common'
        $vdf = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $libs += Join-Path ($_.Matches[0].Groups[1].Value -replace '\\\\', '\') 'steamapps\common' }
        }
    }
    $found = $null
    foreach ($lib in ($libs | Select-Object -Unique)) {
        if (-not (Test-Path $lib)) { continue }
        foreach ($f in Get-ChildItem $lib -Recurse -Filter steam_api.dll -Depth 3 -ErrorAction SilentlyContinue) {
            if ($f.Length -lt 200KB) { continue }
            $b = [IO.File]::ReadAllBytes($f.FullName)
            $pe = [BitConverter]::ToInt32($b, 0x3C)
            if ([BitConverter]::ToUInt16($b, $pe + 4) -eq 0x14C) { $found = $f.FullName; break }
        }
        if ($found) { break }
    }
    if ($found) {
        Copy-Item $found $steamDll -Force
        Write-Host "Steam API copied from $found"
    }
    else {
        Write-Warning 'No modern 32-bit steam_api.dll found in your Steam games. Copy one next to SporeApp.exe as steam_api_coop.dll (ask the host).'
    }
}

$config | Add-Member -NotePropertyName installed -NotePropertyValue $release.tag_name -Force
Save-Config
Write-Host "Updated to $($release.tag_name). Start the game with SporebinEP1\SporeApp.exe."
