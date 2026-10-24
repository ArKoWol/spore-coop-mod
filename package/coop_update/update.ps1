# Updates the Spore co-op mod. Double-click update.bat (it calls this script).
#
# Two sources of news:
#   * the latest GitHub release (the mod build players get): installed into the game folder;
#   * new commits of the repo on GitHub (only when this script lives in a repo checkout): `git pull`.
#
# From a repo checkout it CHECKS both and ASKS what to do (pull the repo, install the release, both, skip).
# On a player's install (<game>\coop_update, no repo) it just installs the latest release, like before.
#
#   update.ps1                        repo checkout: check and ask; player install: install the release
#   update.ps1 -Install               install the latest release without asking (what `launch.ps1 -Mode release` uses)
#   update.ps1 -Check [-Only repo]    check and ask, even from a player install; -Only repo offers only the git pull
#                                     (the launcher uses it: a dev build is built from source, so a release would be overwritten)
# Close the game before installing a release (its DLL is locked while the game runs).
param(
    [string]$GameDir,
    [switch]$Check,
    [switch]$Install,
    [ValidateSet('both', 'repo', 'release')][string]$Only = 'both'
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
# player install: <game>\coop_update\update.ps1 (the parent is the game folder);
# repo checkout: <repo>\package\coop_update\update.ps1 (the repo is the nearest parent with .git)
$parent = Split-Path $here -Parent
$repoRoot = $parent
while ($repoRoot -and -not (Test-Path (Join-Path $repoRoot '.git'))) { $repoRoot = Split-Path $repoRoot -Parent }
$inRepo = [bool]$repoRoot
if (-not $inRepo) { $repoRoot = $parent }
$askMode = $Check -or ($inRepo -and -not $Install)
$quiet = $Check -and $Only -eq 'repo'   # the launcher's check: say nothing unless there is something to decide
$configPath = Join-Path $here 'coop_update.json'
$config = if (Test-Path $configPath) { Get-Content $configPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
$headers = @{ 'User-Agent' = 'spore-coop-updater' }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Save-Config { $config | ConvertTo-Json | Set-Content $configPath -Encoding utf8 }
function Note($text) { Write-Host "[update] $text" -ForegroundColor Cyan }

# --- which GitHub repo
$repoFile = Join-Path $here 'repo.txt'
if (-not $config.repo) {
    if (-not (Test-Path $repoFile)) { throw 'repo.txt is missing next to update.ps1' }
    $config | Add-Member -NotePropertyName repo -NotePropertyValue ((Get-Content $repoFile -Raw).Trim()) -Force
}

# --- which game folder ($null when none is known; asked for only when a release is installed)
function Resolve-GameDir {
    $dir = $GameDir
    if (-not $dir) { $dir = $config.gameDir }
    if (-not $dir -and (Test-Path (Join-Path $parent 'SporebinEP1\SporeApp.exe'))) { $dir = $parent }
    # run from the repo (package\coop_update\update.bat): the modded test copy first, then the game folder (either may be a junction)
    if (-not $dir) {
        $dir = 'test\SPORE_A', 'SPORE' | ForEach-Object { Join-Path $repoRoot $_ } |
            Where-Object { Test-Path "$_\SporebinEP1\SporeApp.exe" } | Select-Object -First 1
    }
    return $dir
}

# --- what is new?
function Get-ReleaseStatus($dir) {
    try {
        $release = Invoke-RestMethod "https://api.github.com/repos/$($config.repo)/releases/latest" -Headers $headers
    }
    catch { Write-Host "[update] could not read the latest release: $($_.Exception.Message)" -ForegroundColor Yellow; return $null }
    $asset = $release.assets | Where-Object name -eq 'coop_package.zip' | Select-Object -First 1
    if (-not $asset) { Write-Host "[update] release $($release.tag_name) has no coop_package.zip" -ForegroundColor Yellow; return $null }
    $installed = $dir -and (Test-Path "$dir\SporeModLoader\ModLibs\CoopMod.dll") -and ($config.installed -eq $release.tag_name)
    return [pscustomobject]@{ Tag = $release.tag_name; Url = $asset.browser_download_url; UpToDate = [bool]$installed }
}

function Get-RepoStatus {
    if (-not $inRepo -or -not (Get-Command git -ErrorAction SilentlyContinue)) { return $null }
    git -C $repoRoot fetch -q origin 2>$null
    if ($LASTEXITCODE -ne 0) { Write-Host '[update] could not reach GitHub with git fetch' -ForegroundColor Yellow; return $null }
    $behind = [int](git -C $repoRoot rev-list --count 'HEAD..origin/main')
    $ahead = [int](git -C $repoRoot rev-list --count 'origin/main..HEAD')
    $commits = @(git -C $repoRoot log --format='%h %an: %s' 'HEAD..origin/main' -n 8)
    $dirty = @(git -C $repoRoot status --porcelain).Count
    return [pscustomobject]@{ Behind = $behind; Ahead = $ahead; Commits = $commits; Dirty = $dirty }
}

# --- actions
function Invoke-Pull($status) {
    Note 'git pull...'
    # git writes its messages to stderr; with ErrorActionPreference=Stop PowerShell would turn them into an exception
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = git -C $repoRoot pull --ff-only 2>&1 | Out-String
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($exit -ne 0) {
        Write-Host $out -ForegroundColor Yellow
        if ($status.Ahead -gt 0) { Write-Host '[update] you have local commits too: run  git pull --rebase  yourself.' -ForegroundColor Yellow }
        elseif ($status.Dirty -gt 0) { Write-Host '[update] you have uncommitted changes that conflict: commit or stash them, then run again.' -ForegroundColor Yellow }
    }
    else { Write-Host $out.Trim() }
}

function Install-Release($release, $dir) {
    if (-not $dir) { $dir = Read-Host 'Path to the Spore game folder (the one that contains SporebinEP1)' }
    $dir = $dir.Trim('"').TrimEnd('\')
    if (-not (Test-Path "$dir\SporebinEP1\SporeApp.exe")) { throw "SporebinEP1\SporeApp.exe not found in $dir" }
    $config | Add-Member -NotePropertyName gameDir -NotePropertyValue $dir -Force
    if (Get-Process SporeApp -ErrorAction SilentlyContinue) { throw 'Spore is running. Close the game and run the update again.' }

    Note "downloading $($release.Tag)..."
    $tmp = Join-Path $env:TEMP "coop_update_$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Force $tmp | Out-Null
    try {
        $zip = Join-Path $tmp 'coop_package.zip'
        Invoke-WebRequest $release.Url -OutFile $zip -Headers $headers
        Expand-Archive $zip (Join-Path $tmp 'pkg') -Force
        $pkg = Join-Path $tmp 'pkg'

        Copy-Item "$pkg\SporebinEP1\dinput8.dll" "$dir\SporebinEP1\" -Force
        New-Item -ItemType Directory -Force "$dir\SporeModLoader\CoreLibs", "$dir\SporeModLoader\ModLibs" | Out-Null
        Copy-Item "$pkg\SporeModLoader\CoreLibs\*" "$dir\SporeModLoader\CoreLibs\" -Force
        Copy-Item "$pkg\SporeModLoader\ModLibs\*" "$dir\SporeModLoader\ModLibs\" -Force
        if (Test-Path "$pkg\Data") { Copy-Item "$pkg\Data\*" "$dir\Data\" -Force }  # CoopMod.package (splash picture)
        if (Test-Path "$pkg\README.txt") { Copy-Item "$pkg\README.txt" "$dir\COOP_README.txt" -Force }
        if (Test-Path "$pkg\fix_spore_registry.ps1") { Copy-Item "$pkg\fix_spore_registry.ps1" $here -Force }
        if (Test-Path "$pkg\docs") { Copy-Item "$pkg\docs" $here -Recurse -Force }  # CLAUDE.md + CHANGELOG.md
    }
    finally {
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    # Steam API: a modern 32-bit steam_api.dll, kept as steam_api_coop.dll (not shipped in the release)
    $steamDll = "$dir\SporebinEP1\steam_api_coop.dll"
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
        if ($found) { Copy-Item $found $steamDll -Force; Note "Steam API copied from $found" }
        else { Write-Warning 'No modern 32-bit steam_api.dll found in your Steam games. Copy one next to SporeApp.exe as steam_api_coop.dll (ask the host).' }
    }

    $config | Add-Member -NotePropertyName installed -NotePropertyValue $release.Tag -Force
    Save-Config
    Note "updated to $($release.Tag). Start the game with SporebinEP1\SporeApp.exe."
}

$dir = Resolve-GameDir

# ---------------------------------------------------------------- player install / -Install: no questions
if (-not $askMode) {
    $release = Get-ReleaseStatus $dir
    if (-not $release) { throw 'Could not read the latest release.' }
    if ($release.UpToDate) { Note "already up to date ($($release.Tag))."; Save-Config; return }
    Install-Release $release $dir
    return
}

# ---------------------------------------------------------------- check and ask
$repo = if ($Only -ne 'release') { Get-RepoStatus } else { $null }
$release = if ($Only -ne 'repo') { Get-ReleaseStatus $dir } else { $null }
$pullNew = $repo -and $repo.Behind -gt 0
$releaseNew = $release -and -not $release.UpToDate

if ($repo) {
    if ($pullNew) {
        Note "the repo has $($repo.Behind) new commit(s) on GitHub:"
        $repo.Commits | ForEach-Object { Write-Host "    $_" }
        if ($repo.Behind -gt $repo.Commits.Count) { Write-Host "    ... and $($repo.Behind - $repo.Commits.Count) more" }
    }
    elseif (-not $quiet) { Note 'repo: up to date with GitHub.' }
    if ($repo.Ahead -gt 0 -and -not $quiet) { Note "you have $($repo.Ahead) local commit(s) that are not on GitHub yet." }
}
if ($release) {
    if ($releaseNew) { Note "release $($release.Tag) is available (installed: $(if ($config.installed) { $config.installed } else { 'none' }))." }
    else { Note "release: $($release.Tag) is installed." }
}

if (-not $pullNew -and -not $releaseNew) { Save-Config; return }

$options = @()
if ($pullNew) { $options += 'P = git pull the new commits' }
if ($releaseNew) { $options += "R = install release $($release.Tag) into the game" }
if ($pullNew -and $releaseNew) { $options += 'B = both' }
$options += 'S = skip (default)'
Write-Host ($options -join '   ')
$letters = ($options | ForEach-Object { $_.Substring(0, 1) }) -join '/'
try { $answer = (Read-Host "Choose $letters").Trim().ToUpper() } catch { $answer = 'S' }

if ($answer -in 'P', 'B' -and $pullNew) { Invoke-Pull $repo }
if ($answer -in 'R', 'B' -and $releaseNew) { Install-Release $release $dir }
if ($answer -notin 'P', 'R', 'B') { Note 'skipped.' }
Save-Config
