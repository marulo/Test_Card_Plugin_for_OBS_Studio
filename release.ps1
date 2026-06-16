# release.ps1 - Bump version across all required files and push a new release tag
# Usage: .\release.ps1 <version>
# Example: .\release.ps1 0.2.3

param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"

Write-Host "==> Releasing version $Version" -ForegroundColor Cyan

# 1. buildspec.json
Write-Host "  Updating buildspec.json..."
$content = Get-Content buildspec.json -Raw
if ($content -notmatch '"version": "\d+\.\d+\.\d+"') {
    Write-Error "Could not find plugin version in buildspec.json"
    exit 1
}
$content = $content -replace '("name": "test-card"[\s\S]*?"version": ")[^"]+(")', "`${1}$Version`${2}"
Set-Content buildspec.json $content -NoNewline

# 2. src/test-source.c  (version string shown in the plugin UI)
Write-Host "  Updating src/test-source.c..."
$content = Get-Content src/test-source.c -Raw
$content = $content -replace 'Test Card Plugin V\. \d+\.\d+\.\d+', "Test Card Plugin V. $Version"
Set-Content src/test-source.c $content -NoNewline

Write-Host "  Verifying changes..."
Select-String "version" buildspec.json | Where-Object { $_.Line -match "test-card|0\." } | ForEach-Object { Write-Host "    buildspec: $($_.Line.Trim())" -ForegroundColor Green }
Select-String "Test Card Plugin V\." src/test-source.c | ForEach-Object { Write-Host "    test-source.c: $($_.Line.Trim())" -ForegroundColor Green }

# 3. git add, commit, tag, push
Write-Host "  Committing..."
git commit -a -m "Release $Version"
git tag $Version
git push origin master --tags

Write-Host ""
Write-Host "==> Done! Version $Version pushed and tagged." -ForegroundColor Green
Write-Host "    GitHub Actions will now build and publish the release automatically."
