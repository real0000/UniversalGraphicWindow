<#
.SYNOPSIS
    Project wizard for UniversalGraphicWindow (Windows / PowerShell).

.DESCRIPTION
    Creates a new cross-platform CMake project that consumes UGW either by
    referencing this checkout in-place or by adding it as a git submodule
    under <project>/UGW.

.EXAMPLE
    pwsh ./setup/wizard.ps1
    pwsh ./setup/wizard.ps1 -ProjectName MyApp -ProjectPath C:\src\MyApp -Mode submodule
#>

[CmdletBinding()]
param(
    [string]$ProjectName,
    [string]$ProjectPath,
    [ValidateSet('reference', 'submodule')]
    [string]$Mode,
    [string]$RepoUrl,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$UgwRoot    = Resolve-Path (Join-Path $ScriptRoot '..')
$Templates  = Join-Path $ScriptRoot 'templates'

function Read-Default {
    param([string]$Prompt, [string]$Default)
    if ($Default) { $reply = Read-Host "$Prompt [$Default]" }
    else          { $reply = Read-Host  $Prompt }
    if ([string]::IsNullOrWhiteSpace($reply)) { return $Default }
    return $reply
}

function Read-Choice {
    param([string]$Prompt, [string[]]$Options, [string]$Default)
    while ($true) {
        $reply = Read-Default -Prompt "$Prompt ($($Options -join '/'))" -Default $Default
        if ($Options -contains $reply) { return $reply }
        Write-Host "Please choose one of: $($Options -join ', ')" -ForegroundColor Yellow
    }
}

function Expand-Template {
    param([string]$Source, [string]$Destination, [hashtable]$Vars)
    $text = Get-Content -Raw -LiteralPath $Source
    foreach ($key in $Vars.Keys) {
        $text = $text.Replace("@$key@", [string]$Vars[$key])
    }
    $dir = Split-Path -Parent $Destination
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    Set-Content -LiteralPath $Destination -Value $text -Encoding utf8
}

function Get-OriginUrl {
    try {
        $url = & git -C $UgwRoot remote get-url origin 2>$null
        if ($LASTEXITCODE -eq 0 -and $url) { return $url.Trim() }
    } catch { }
    return $null
}

# ---------------------------------------------------------------------------
Write-Host "UniversalGraphicWindow project wizard" -ForegroundColor Cyan
Write-Host "  UGW source: $UgwRoot"
Write-Host ""

if (-not $ProjectName) { $ProjectName = Read-Default -Prompt 'Project name' -Default 'MyApp' }
if ($ProjectName -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
    throw "Project name '$ProjectName' is not a valid C identifier."
}

if (-not $ProjectPath) {
    $defaultPath = Join-Path (Get-Location).Path $ProjectName
    $ProjectPath = Read-Default -Prompt 'Project path' -Default $defaultPath
}
$ProjectPath = [System.IO.Path]::GetFullPath($ProjectPath)

if (-not $Mode) {
    $Mode = Read-Choice -Prompt 'Integrate UGW as' -Options @('reference', 'submodule') -Default 'reference'
}

if ($Mode -eq 'submodule' -and -not $RepoUrl) {
    $detected = Get-OriginUrl
    $RepoUrl  = Read-Default -Prompt 'UGW git URL' -Default $detected
    if (-not $RepoUrl) { throw 'A git URL is required for submodule mode.' }
}

# ---------------------------------------------------------------------------
if (Test-Path -LiteralPath $ProjectPath) {
    if (-not $Force) {
        $items = @(Get-ChildItem -Force -LiteralPath $ProjectPath)
        if ($items.Count -gt 0) {
            throw "Target path '$ProjectPath' is not empty (use -Force to overwrite)."
        }
    }
} else {
    New-Item -ItemType Directory -Path $ProjectPath -Force | Out-Null
}

Write-Host ""
Write-Host "Creating project '$ProjectName' at $ProjectPath ($Mode mode)..." -ForegroundColor Green

# Initialise git repo (needed for submodule mode; harmless otherwise).
Push-Location $ProjectPath
try {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectPath '.git'))) {
        & git init --quiet
        if ($LASTEXITCODE -ne 0) { throw 'git init failed.' }
    }

    if ($Mode -eq 'submodule') {
        $sub = Join-Path $ProjectPath 'UGW'
        if (Test-Path -LiteralPath $sub) {
            if ($Force) { Remove-Item -Recurse -Force -LiteralPath $sub }
            else { throw "Submodule directory '$sub' already exists." }
        }
        & git submodule add $RepoUrl UGW
        if ($LASTEXITCODE -ne 0) { throw 'git submodule add failed.' }
        & git submodule update --init --recursive
        $UgwPathForCmake = '${CMAKE_SOURCE_DIR}/UGW'
    } else {
        $UgwPathForCmake = ($UgwRoot.Path -replace '\\', '/')
    }
} finally {
    Pop-Location
}

$vars = @{
    PROJECT_NAME = $ProjectName
    UGW_PATH     = $UgwPathForCmake
    UGW_MODE     = $Mode
}

Expand-Template -Source (Join-Path $Templates 'CMakeLists.txt.in') `
                -Destination (Join-Path $ProjectPath 'CMakeLists.txt') `
                -Vars $vars

Expand-Template -Source (Join-Path $Templates 'main.cpp.in') `
                -Destination (Join-Path $ProjectPath 'src/main.cpp') `
                -Vars $vars

Set-Content -LiteralPath (Join-Path $ProjectPath '.gitignore') -Encoding utf8 -Value @"
build/
out/
.vs/
.vscode/
*.user
"@

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  cd `"$ProjectPath`""
Write-Host "  cmake -S . -B build"
Write-Host "  cmake --build build --config Release"
