# One-time developer setup: build tools, Spore ModAPI SDK, SporeModLoader, then builds the mod.
# Usage:  .\setup.ps1 [-GameDir "D:\Games\SPORE"]
#   -GameDir  your Spore folder (the one containing SporebinEP1); makes a test copy in test\SPORE_A with the mod installed.
# Needs: Windows 10/11, winget (for the Build Tools), an internet connection. Run in a normal PowerShell window.
param([string]$GameDir)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dl = Join-Path $root 'downloads'
New-Item -ItemType Directory -Force $dl | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$headers = @{ 'User-Agent' = 'spore-coop-setup' }

function Get-VsPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        return (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    }
}

# 1. Visual Studio Build Tools (C++ x86)
if (-not (Get-VsPath)) {
    Write-Host 'Installing Visual Studio 2022 Build Tools (a Windows admin prompt will appear)...'
    winget install --id Microsoft.VisualStudio.2022.BuildTools --source winget --accept-package-agreements --accept-source-agreements `
        --override '--passive --norestart --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    if (-not (Get-VsPath)) { throw 'Build Tools are still missing after the install.' }
}
$vs = Get-VsPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'

# 2. Spore ModAPI SDK (source)
$sdkRoot = Join-Path $dl 'Spore-ModAPI-src\Spore-ModAPI-master'
if (-not (Test-Path $sdkRoot)) {
    Write-Host 'Downloading the Spore ModAPI SDK (about 70 MB)...'
    $zip = Join-Path $dl 'Spore-ModAPI.zip'
    Invoke-WebRequest 'https://codeload.github.com/Spore-Community/Spore-ModAPI/zip/refs/heads/master' -OutFile $zip -Headers $headers
    Expand-Archive $zip (Join-Path $dl 'Spore-ModAPI-src') -Force
    Remove-Item $zip
}

# 3. SDK libraries the mod links against
if (-not ((Test-Path "$sdkRoot\lib\Release\SporeModAPIBase.lib") -and (Test-Path "$sdkRoot\dll\Release\SporeModAPI.lib"))) {
    Write-Host 'Building the SDK libraries (a minute or two)...'
    $cmd = "call `"$vcvars`" >nul && cd /d `"$sdkRoot`" && " +
        "msbuild `"Spore ModAPI`" -t:BuildDlls -p:BuildVer=177 -p:Config=Release -v:q -nologo && " +
        "cd /d `"$sdkRoot\Spore ModAPI`" && " +
        "msbuild `"Spore ModAPI.vcxproj`" -p:Configuration=Release -p:Platform=Win32 -p:SDK_BUILD_VER=177 -p:EXECUTABLE_TYPE=0 -v:q -nologo"
    cmd /c $cmd
    if (-not ((Test-Path "$sdkRoot\lib\Release\SporeModAPIBase.lib") -and (Test-Path "$sdkRoot\dll\Release\SporeModAPI.lib"))) {
        throw 'SDK library build failed.'
    }
}

# 4. SporeModLoader (used by make_package.ps1)
if (-not (Get-ChildItem $dl -Directory -Filter 'SporeModLoader-*' -ErrorAction SilentlyContinue)) {
    Write-Host 'Downloading SporeModLoader...'
    $rel = Invoke-RestMethod 'https://api.github.com/repos/Rosalie241/SporeModLoader/releases/tags/rolling-release' -Headers $headers
    $asset = $rel.assets | Where-Object { $_.name -like 'SporeModLoader-*.zip' -and $_.name -notlike '*macos*' } | Select-Object -First 1
    $name = [IO.Path]::GetFileNameWithoutExtension($asset.name)
    $zip = Join-Path $dl $asset.name
    Invoke-WebRequest $asset.browser_download_url -OutFile $zip -Headers $headers
    Expand-Archive $zip (Join-Path $dl $name) -Force
    Remove-Item $zip
}

# 5. build the mod
Write-Host 'Building CoopMod...'
& (Join-Path $root 'mod\CoopMod\build.bat')
if (-not (Test-Path (Join-Path $root 'mod\CoopMod\staging\CoopMod.dll'))) { throw 'The mod build failed.' }

# 6. optional test copy of the game
if ($GameDir) {
    $GameDir = $GameDir.Trim('"').TrimEnd('\')
    if (-not (Test-Path "$GameDir\SporebinEP1\SporeApp.exe")) { throw "SporebinEP1\SporeApp.exe not found in $GameDir" }
    $copy = Join-Path $root 'test\SPORE_A'
    if (-not (Test-Path "$copy\SporebinEP1\SporeApp.exe")) {
        Write-Host "Copying the game to $copy (about 6 GB)..."
        robocopy $GameDir $copy /E /NFL /NDL /NJH /NP /MT:8 | Out-Null
    }
    Write-Host 'Installing the loader, the Steam API file and the latest release into the test copy...'
    & (Join-Path $root 'update\update.ps1') -GameDir $copy
}

Write-Host ''
Write-Host 'Done. Next:'
Write-Host '  - change mod\CoopMod\dllmain.cpp, run mod\CoopMod\build.bat'
if ($GameDir) { Write-Host '  - launch_spore_mod.bat builds, installs the new build into the test copy and starts the game' }
Write-Host '  - publish.ps1 builds the package and publishes a GitHub release (needs gh auth login)'
