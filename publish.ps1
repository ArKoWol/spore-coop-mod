# Builds the package and publishes it as a GitHub release (needs: gh auth login, a remote named origin).
# Usage: .\publish.ps1 [-Notes "what changed"]   (run the mod build first so staging\CoopMod.dll is current)
param([string]$Notes = 'Co-op mod build')

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$repo = (gh repo view --json nameWithOwner --jq .nameWithOwner)
if (-not $repo) { throw 'No GitHub repo found: create it first (gh repo create) and add the origin remote.' }

& "$PSScriptRoot\make_package.ps1" -Repo $repo | Select-Object -Last 1
$tag = 'build-' + (Get-Date -Format 'yyyyMMdd-HHmm')
gh release create $tag "$PSScriptRoot\coop_package.zip" --repo $repo --title $tag --notes $Notes --latest
"Published $tag to $repo"
