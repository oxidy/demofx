# One-time: install project GitHub Packages credentials where VS Code / m2e look.
# After this, bumping <demofx.version> in pom.xml downloads like a normal Maven dep.
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Src = Join-Path $Root ".mvn\settings.xml"
$DestDir = Join-Path $env:USERPROFILE ".m2"
$Dest = Join-Path $DestDir "settings.xml"

if (-not (Test-Path $Src)) {
    Write-Error "Missing $Src"
}

New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

if (Test-Path $Dest) {
    $backup = "$Dest.bak.$(Get-Date -Format 'yyyyMMddHHmmss')"
    Copy-Item $Dest $backup
    Write-Host "Backed up existing settings to $backup"
}

Copy-Item $Src $Dest -Force
Write-Host "Installed Maven settings -> $Dest"
Write-Host "Reload VS Code / run: Java: Clean Java Language Server Workspace"
Write-Host "Then bump demofx.version in pom.xml as usual."
