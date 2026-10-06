[CmdletBinding()]
param(
    [switch] $Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Find-ClangFormat {
    $command = Get-Command "clang-format.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $installerRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    $vswhere = Join-Path $installerRoot "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "clang-format.exe was not found. Install the Visual Studio C++ Clang tools."
    }

    $installation = & $vswhere -latest -products * -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw "Visual Studio was not found."
    }

    $candidates = @(
        (Join-Path $installation.Trim() "VC\Tools\Llvm\x64\bin\clang-format.exe"),
        (Join-Path $installation.Trim() "VC\Tools\Llvm\bin\clang-format.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "clang-format.exe was not found. Install the Visual Studio C++ Clang tools."
}

$clangFormat = Find-ClangFormat
$targets = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "src") -Recurse -File |
    Where-Object {
        $_.Extension -in ".cpp", ".h" -and
        $_.FullName -notlike "*\src\generated\*"
    } |
    Sort-Object FullName

if ($targets.Count -eq 0) {
    throw "No first-party C++ files were found."
}

$batchSize = 32
for ($first = 0; $first -lt $targets.Count; $first += $batchSize) {
    $last = [Math]::Min($first + $batchSize - 1, $targets.Count - 1)
    $batch = @($targets[$first..$last].FullName)
    if ($Check) {
        & $clangFormat --style=file --fallback-style=none --dry-run --Werror $batch
    }
    else {
        & $clangFormat --style=file --fallback-style=none -i $batch
    }

    if ($LASTEXITCODE -ne 0) {
        $operation = if ($Check) { "check" } else { "format" }
        throw "clang-format $operation failed with exit code $LASTEXITCODE."
    }
}

$verb = if ($Check) { "verified" } else { "formatted" }
Write-Host "Source formatting $verb for $($targets.Count) first-party files."
