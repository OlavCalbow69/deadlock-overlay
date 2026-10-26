param([string]$DotnetPath = '', [switch]$RebuildDataTools, [switch]$SelfTests)
$ErrorActionPreference = 'Stop'
$buildDirectory = Join-Path $PSScriptRoot 'build'
cmake -S $PSScriptRoot -B $buildDirectory -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $buildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$releaseDirectory = Join-Path $buildDirectory 'Release'
$updaterExecutable = Join-Path $releaseDirectory 'tools\data-update\DeadlockDataUpdate.exe'
$dumperExecutable = Join-Path $releaseDirectory 'tools\schema-dumper\dezlock-dump.exe'
if ($RebuildDataTools -or !(Test-Path -LiteralPath $updaterExecutable) -or !(Test-Path -LiteralPath $dumperExecutable)) {
    & (Join-Path $PSScriptRoot 'tools\build-data-tools.ps1') -DotnetPath $DotnetPath -SelfTests:$SelfTests
    # Register the updater check on a fresh checkout after its executable exists.
    cmake -S $PSScriptRoot -B $buildDirectory -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake test configuration failed.' }
}
$mapDirectory = Join-Path $PSScriptRoot 'maps'
if (Test-Path -LiteralPath $mapDirectory) {
    Copy-Item -LiteralPath $mapDirectory -Destination $releaseDirectory -Recurse -Force
}
$iconDirectory = Join-Path $PSScriptRoot 'data\icons'
if (Test-Path -LiteralPath $iconDirectory) {
    New-Item -ItemType Directory -Path (Join-Path $releaseDirectory 'data') -Force | Out-Null
    Copy-Item -LiteralPath $iconDirectory -Destination (Join-Path $releaseDirectory 'data') -Recurse -Force
}
$previewDirectory = Join-Path $PSScriptRoot 'data\preview'
if (Test-Path -LiteralPath $previewDirectory) {
    New-Item -ItemType Directory -Path (Join-Path $releaseDirectory 'data') -Force | Out-Null
    Copy-Item -LiteralPath $previewDirectory -Destination (Join-Path $releaseDirectory 'data') -Recurse -Force
}
$laneDirectory = Join-Path $PSScriptRoot 'data\map-lanes'
if (Test-Path -LiteralPath $laneDirectory) {
    New-Item -ItemType Directory -Path (Join-Path $releaseDirectory 'data') -Force | Out-Null
    Copy-Item -LiteralPath $laneDirectory -Destination (Join-Path $releaseDirectory 'data') -Recurse -Force
}
if ($SelfTests) {
    ctest --test-dir $buildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
} else {
    Write-Output 'Build complete. No checks were requested.'
}
Write-Output (Join-Path $buildDirectory 'Release\DeadlockOverlay.exe')
