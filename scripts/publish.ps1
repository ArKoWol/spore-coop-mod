# Builds the package and publishes it as a GitHub release (needs: gh auth login, a remote named origin).
# Usage: .\scripts\publish.ps1 [-Notes "override text"]   (build the mod first so staging\CoopMod.dll is current)
# The release notes come from the "## Unreleased" section of CHANGELOG.md; afterwards that section is moved under the new tag.
# CLAUDE.md and CHANGELOG.md are attached to the release and shipped inside the package (docs\).
param([string]$Notes)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent  # the repo (this script lives in scripts\)
Set-Location $root

$repo = (gh repo view --json nameWithOwner --jq .nameWithOwner)
if (-not $repo) { throw 'No GitHub repo found: create it first (gh repo create) and add the origin remote.' }

$tag = 'build-' + (Get-Date -Format 'yyyyMMdd-HHmm')
$changelogPath = Join-Path $root 'CHANGELOG.md'
$changelog = Get-Content $changelogPath -Raw

if (-not $Notes) {
    if ($changelog -match '(?s)## Unreleased\s*\r?\n(.*?)(?=\r?\n## |\z)') { $Notes = $Matches[1].Trim() }
    if (-not $Notes) { throw 'CHANGELOG.md has nothing under "## Unreleased". Record what changed first (see CLAUDE.md).' }
}

# move the Unreleased entries under the new tag
$changelog = [regex]::Replace($changelog, '(?m)^## Unreleased', "## Unreleased`r`n`r`n## $tag", 1)
Set-Content $changelogPath $changelog -Encoding utf8

& "$PSScriptRoot\make_package.ps1" -Repo $repo | Select-Object -Last 1
$notesFile = Join-Path $env:TEMP "coop_release_notes.md"
Set-Content $notesFile $Notes -Encoding utf8
gh release create $tag "$root\coop_package.zip" "$root\CLAUDE.md" "$root\CHANGELOG.md" --repo $repo --title $tag --notes-file $notesFile --latest
if ($LASTEXITCODE -ne 0) { throw "gh release create failed (exit $LASTEXITCODE). CHANGELOG.md was already edited: undo with git checkout CHANGELOG.md." }
"Published $tag to $repo. CHANGELOG.md was updated: commit and push it."


