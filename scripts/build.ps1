[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release",

    [switch] $Rebuild,
    [switch] $Run,
    [switch] $StopRunning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot "Solace.sln"
$applicationPath = Join-Path $repositoryRoot "$Configuration\Solace.exe"
$layoutVerifier = Join-Path $PSScriptRoot "verify-source-layout.ps1"

function Find-MSBuild {
    $command = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $installerRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    $vswhere = Join-Path $installerRoot "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installation) {
            $candidate = Join-Path $installation.Trim() "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw "MSBuild.exe was not found. Install Visual Studio 2022 with Desktop development with C++."
}

if ($StopRunning -and (Test-Path -LiteralPath $applicationPath)) {
    $resolvedApplication = [IO.Path]::GetFullPath($applicationPath)
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ExecutablePath -and
            [IO.Path]::GetFullPath($_.ExecutablePath) -ieq $resolvedApplication
        } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
}

$msbuild = Find-MSBuild
$target = if ($Rebuild) { "Rebuild" } else { "Build" }

Write-Host "Verifying source layout..."
& $layoutVerifier

Write-Host "Building Solace ($Configuration|x64)..."
& $msbuild $solutionPath "/t:$target" "/p:Configuration=$Configuration" "/p:Platform=x64" `
    "/m" "/nologo" "/v:minimal"
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if ($Run) {
    if (-not (Test-Path -LiteralPath $applicationPath)) {
        throw "The application executable was not produced: $applicationPath"
    }
    Start-Process -FilePath $applicationPath -WorkingDirectory $repositoryRoot
}

Write-Host "Build completed successfully."
