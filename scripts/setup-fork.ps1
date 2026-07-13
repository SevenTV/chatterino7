param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$GitHubUsername,

    [Parameter(Mandatory = $false)]
    [ValidateNotNullOrEmpty()]
    [string]$Repository = "chatterino7-twitch-kick"
)

$ErrorActionPreference = "Stop"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Fant ikke '$Name'. Installer programmet og prøv igjen."
    }
}

Require-Command "git"
Require-Command "gh"

$sourceRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$targetRoot = Join-Path (Split-Path $sourceRoot -Parent) $Repository

& gh auth status
if ($LASTEXITCODE -ne 0) {
    throw "GitHub CLI er ikke innlogget. Kjør: gh auth login"
}

if (Test-Path $targetRoot) {
    throw "Målmappen finnes allerede: $targetRoot"
}

Write-Host "Oppretter ekte fork av SevenTV/chatterino7 ..."
& gh repo fork SevenTV/chatterino7 --fork-name $Repository --clone --default-branch-only
if ($LASTEXITCODE -ne 0) {
    throw "Kunne ikke opprette eller klone forken."
}

$clonedDefault = Join-Path (Get-Location) $Repository
if (-not (Test-Path $clonedDefault)) {
    throw "Fant ikke den klonede mappen: $clonedDefault"
}

$filesToCopy = @(
    ".github/workflows/build.yml",
    "src/widgets/dialogs/SelectChannelDialog.cpp",
    "FORK_SETUP_NO.md",
    "README_FORK_NO.md",
    "scripts/setup-fork.ps1"
)

foreach ($relative in $filesToCopy) {
    $source = Join-Path $sourceRoot $relative
    $destination = Join-Path $clonedDefault $relative
    $destinationDirectory = Split-Path $destination -Parent
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
    Copy-Item $source $destination -Force
}

Set-Location $clonedDefault
& git checkout -B main
& git add --all
& git commit -m "Configure Twitch and Kick multichannel fork"
if ($LASTEXITCODE -ne 0) {
    throw "Kunne ikke committe endringene. Kontroller Git-navn og e-post."
}

& git push -u origin main
if ($LASTEXITCODE -ne 0) {
    throw "Kunne ikke pushe main-grenen."
}

Write-Host "Ferdig: https://github.com/$GitHubUsername/$Repository"
Write-Host "Prosjektmappe: $clonedDefault"
