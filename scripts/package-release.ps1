[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version = "0.1.0",

    [string] $OutputDirectory = "artifacts\releases",

    [switch] $SkipBuild,

    [switch] $ValidateStartup
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$productInfoPath = Join-Path $repositoryRoot "src\core\product_info.h"
$productInfoSource = Get-Content -LiteralPath $productInfoPath -Raw
$versionMatch = [Text.RegularExpressions.Regex]::Match(
    $productInfoSource,
    'inline\s+constexpr\s+char\s+version\[\]\s*=\s*"([^"]+)"')
if (-not $versionMatch.Success) {
    throw "Could not read the product version from $productInfoPath."
}
$sourceVersion = $versionMatch.Groups[1].Value
if ($Version -cne $sourceVersion) {
    throw "Package version '$Version' does not match source version '$sourceVersion'."
}

$packageName = "Solace-$Version-win64"
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
}
else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$archivePath = Join-Path $outputRoot "$packageName.zip"
$hashPath = "$archivePath.sha256"
$temporaryArchive = Join-Path $outputRoot (".{0}.{1}.tmp" -f $packageName, [Guid]::NewGuid())

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -Configuration Release -Rebuild -StopRunning
}

$applicationPath = Join-Path $repositoryRoot "Release\Solace.exe"
$assetsRoot = Join-Path $repositoryRoot "assets"
$entries = [System.Collections.Generic.List[object]]::new()

if (-not (Test-Path -LiteralPath $applicationPath -PathType Leaf)) {
    throw "The release executable was not produced: $applicationPath"
}
$executableVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($applicationPath).ProductVersion
if ([string]::IsNullOrWhiteSpace($executableVersion) -or
    $executableVersion.Trim() -cne $Version) {
    throw "Package version '$Version' does not match executable product version '$executableVersion'."
}

function Add-PackageEntry {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required release file was not found: $Source"
    }

    $entries.Add([PSCustomObject]@{
        Source = [IO.Path]::GetFullPath($Source)
        Destination = $Destination.Replace("\", "/")
    })
}

Add-PackageEntry $applicationPath "Solace.exe"
Add-PackageEntry (Join-Path $repositoryRoot "LICENSE") "LICENSE"
Add-PackageEntry (Join-Path $repositoryRoot "README.md") "README.md"
Add-PackageEntry (Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md") "THIRD_PARTY_NOTICES.md"
Add-PackageEntry (Join-Path $repositoryRoot "docs\imgui-patches.md") "docs/imgui-patches.md"
Add-PackageEntry (Join-Path $repositoryRoot "docs\img\menu.png") "docs/img/menu.png"
Add-PackageEntry (Join-Path $repositoryRoot "docs\media\signup-light.png") `
    "docs/media/signup-light.png"
Add-PackageEntry (Join-Path $repositoryRoot "docs\media\signup-dark.png") `
    "docs/media/signup-dark.png"
Add-PackageEntry (Join-Path $repositoryRoot "docs\media\video-preview.jpg") `
    "docs/media/video-preview.jpg"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\freetype\LICENSE.TXT") `
    "thirdparty/freetype/LICENSE.TXT"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\freetype\FTL.TXT") `
    "thirdparty/freetype/FTL.TXT"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\freetype\GPLv2.TXT") `
    "thirdparty/freetype/GPLv2.TXT"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\freetype\BDF-README.txt") `
    "thirdparty/freetype/BDF-README.txt"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\freetype\PCF-README.txt") `
    "thirdparty/freetype/PCF-README.txt"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\freetype\HARFBUZZ-LICENSE.txt") `
    "thirdparty/freetype/HARFBUZZ-LICENSE.txt"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\geist\LICENSE.txt") `
    "thirdparty/geist/LICENSE.txt"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\imgui\LICENSE.txt") `
    "thirdparty/imgui/LICENSE.txt"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\lucide\LICENSE") `
    "thirdparty/lucide/LICENSE"
Add-PackageEntry (Join-Path $repositoryRoot "thirdparty\stb\LICENSE.txt") `
    "thirdparty/stb/LICENSE.txt"

if (-not (Test-Path -LiteralPath $assetsRoot -PathType Container)) {
    throw "The runtime assets directory was not found: $assetsRoot"
}

foreach ($asset in @(Get-ChildItem -LiteralPath $assetsRoot -Recurse -File)) {
    $relativePath = $asset.FullName.Substring($assetsRoot.Length + 1).Replace("\", "/")
    Add-PackageEntry $asset.FullName "assets/$relativePath"
}

$duplicateDestinations = @($entries | Group-Object Destination | Where-Object Count -gt 1)
if ($duplicateDestinations.Count -gt 0) {
    throw "Duplicate archive destination: $($duplicateDestinations[0].Name)"
}

$entries = @($entries | Sort-Object Destination)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$fileStream = $null
$archive = $null
try {
    try {
        $fileStream = [IO.File]::Open(
            $temporaryArchive,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        $archive = [IO.Compression.ZipArchive]::new(
            $fileStream,
            [IO.Compression.ZipArchiveMode]::Create,
            $false)
        $timestamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)

        foreach ($item in $entries) {
            $archiveEntry = $archive.CreateEntry(
                "$packageName/$($item.Destination)",
                [IO.Compression.CompressionLevel]::Optimal)
            $archiveEntry.LastWriteTime = $timestamp
            $archiveEntry.ExternalAttributes = 0

            $input = $null
            $output = $null
            try {
                $input = [IO.File]::OpenRead($item.Source)
                $output = $archiveEntry.Open()
                $input.CopyTo($output)
            }
            finally {
                if ($output) { $output.Dispose() }
                if ($input) { $input.Dispose() }
            }
        }
    }
    finally {
        if ($archive) { $archive.Dispose() }
        if ($fileStream) { $fileStream.Dispose() }
    }

    if (Test-Path -LiteralPath $archivePath) {
        [IO.File]::Delete($archivePath)
    }
    [IO.File]::Move($temporaryArchive, $archivePath)
}
finally {
    if (Test-Path -LiteralPath $temporaryArchive) {
        [IO.File]::Delete($temporaryArchive)
    }
}

$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(
    $hashPath,
    "$hash  $([IO.Path]::GetFileName($archivePath))`r`n",
    [Text.Encoding]::ASCII)

if ($ValidateStartup) {
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $temporaryRoot.EndsWith([IO.Path]::DirectorySeparatorChar.ToString())) {
        $temporaryRoot += [IO.Path]::DirectorySeparatorChar
    }
    $validationRoot = [IO.Path]::GetFullPath((
        Join-Path $temporaryRoot ("solace-release-{0}" -f [Guid]::NewGuid())))
    if (-not $validationRoot.StartsWith(
            $temporaryRoot,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use an unexpected validation directory: $validationRoot"
    }

    $process = $null
    try {
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $validationRoot)
        $packagedExecutable = Join-Path $validationRoot "$packageName\Solace.exe"
        $packagedWorkingDirectory = Split-Path -Parent $packagedExecutable
        $process = Start-Process -FilePath $packagedExecutable `
            -WorkingDirectory $packagedWorkingDirectory -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 3
        $process.Refresh()
        if ($process.HasExited) {
            throw "The packaged application exited during startup validation (exit code $($process.ExitCode))."
        }
    }
    finally {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
        if (Test-Path -LiteralPath $validationRoot) {
            Remove-Item -LiteralPath $validationRoot -Recurse -Force
        }
    }
}

Write-Host "Release package: $archivePath"
Write-Host "SHA-256: $hash"
Write-Host "Included files: $($entries.Count)"
