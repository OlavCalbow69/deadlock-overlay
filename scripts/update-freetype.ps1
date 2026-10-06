[CmdletBinding()]
param(
    [string] $ArchivePath,

    [switch] $KeepWorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$version = "2.14.3"
$archiveName = "ft2143.zip"
$archiveUrl = "https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/ft2143.zip"
$releaseUrl = "https://sourceforge.net/projects/freetype/files/freetype2/2.14.3/"
$expectedArchiveSha256 = "566518a6d5a5cfbfc9697fca5b59571de5a357f9d6fe41c7cf8adc80c4b0bd06"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$thirdPartyRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "thirdparty"))
$vendorRoot = [IO.Path]::GetFullPath((Join-Path $thirdPartyRoot "freetype"))
$localDataRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$workRoot = Join-Path $localDataRoot ("Solace\FreeTypeUpdate\" + [guid]::NewGuid().ToString("N"))
$incomingRoot = $null
$backupRoot = $null

function Assert-ExpectedRepository {
    $gitDirectory = Join-Path $repositoryRoot ".git"
    if (-not (Test-Path -LiteralPath $gitDirectory)) {
        throw "Repository root was not found: $repositoryRoot"
    }

    $expectedVendorRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "thirdparty\freetype"))
    if ($vendorRoot -ne $expectedVendorRoot -or -not $vendorRoot.StartsWith($thirdPartyRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an unexpected vendor path: $vendorRoot"
    }
}

function Find-CMake {
    $command = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "cmake.exe was not found. Install CMake and make it available on PATH."
}

function Find-VisualStudioInstallation {
    $installerRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    $vswhere = Join-Path $installerRoot "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe was not found. Install Visual Studio 2022 with Desktop development with C++."
    }

    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw "Visual Studio 2022 with the x64 C++ toolchain was not found."
    }

    return $installation.Trim()
}

function Find-DumpBin([string] $VisualStudioRoot) {
    $toolsetsRoot = Join-Path $VisualStudioRoot "VC\Tools\MSVC"
    $toolset = Get-ChildItem -LiteralPath $toolsetsRoot -Directory |
        Sort-Object { [version] $_.Name } -Descending |
        Select-Object -First 1
    if (-not $toolset) {
        throw "No MSVC toolset was found under $toolsetsRoot."
    }

    $dumpbin = Join-Path $toolset.FullName "bin\Hostx64\x64\dumpbin.exe"
    if (-not (Test-Path -LiteralPath $dumpbin)) {
        throw "dumpbin.exe was not found: $dumpbin"
    }

    return $dumpbin
}

function Invoke-Checked([string] $Description, [string] $Command, [string[]] $Arguments) {
    Write-Host $Description
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Read-CMakeCompilerVersion([string] $BuildRoot) {
    $compilerFile = Get-ChildItem -LiteralPath (Join-Path $BuildRoot "CMakeFiles") `
        -Filter "CMakeCCompiler.cmake" -File -Recurse |
        Select-Object -First 1
    if (-not $compilerFile) {
        throw "CMake compiler metadata was not generated."
    }

    $entry = Select-String -LiteralPath $compilerFile.FullName `
        -Pattern '^set\(CMAKE_C_COMPILER_VERSION "([^"]+)"\)' |
        Select-Object -First 1
    if (-not $entry) {
        throw "The MSVC compiler version was not found in $($compilerFile.FullName)."
    }

    return $entry.Matches[0].Groups[1].Value
}

function Read-MSBuildProperty([string] $ProjectPath, [string] $Name) {
    $project = Get-Content -LiteralPath $ProjectPath -Raw
    $match = [regex]::Match($project, "<" + [regex]::Escape($Name) + ">([^<]+)</" + [regex]::Escape($Name) + ">")
    if (-not $match.Success) {
        throw "MSBuild property '$Name' was not found in $ProjectPath."
    }

    return $match.Groups[1].Value
}

function Write-Utf8File([string] $Path, [string] $Content) {
    $encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Assert-NoAbsoluteBuildPaths([string] $LibraryPath, [string[]] $ForbiddenMarkers) {
    $bytes = [IO.File]::ReadAllBytes($LibraryPath)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    $unicode = [Text.Encoding]::Unicode.GetString($bytes)
    $unicodeOdd = if ($bytes.Length -gt 1) {
        [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1)
    }
    else {
        ""
    }

    foreach ($marker in $ForbiddenMarkers | Where-Object { $_ }) {
        if ($ascii.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $unicode.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $unicodeOdd.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "The generated FreeType library leaks a private build-path marker: $marker"
        }
    }

    if ($ascii -match '(?i)[a-z]:[\\/]' -or
        $unicode -match '(?i)[a-z]:[\\/]' -or
        $unicodeOdd -match '(?i)[a-z]:[\\/]') {
        throw "The generated FreeType library contains an absolute Windows path."
    }
}

Assert-ExpectedRepository
$cmake = Find-CMake
$visualStudioRoot = Find-VisualStudioInstallation
$dumpbin = Find-DumpBin $visualStudioRoot
$libTool = Join-Path (Split-Path -Parent $dumpbin) "lib.exe"
if (-not (Test-Path -LiteralPath $libTool)) {
    throw "lib.exe was not found next to dumpbin.exe: $libTool"
}
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null

try {
    $localArchive = Join-Path $workRoot $archiveName
    if ($ArchivePath) {
        $resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
        Write-Host "Using local FreeType archive: $resolvedArchive"
        Copy-Item -LiteralPath $resolvedArchive -Destination $localArchive
    }
    else {
        Write-Host "Downloading FreeType $version from the official release mirror..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $localArchive -UseBasicParsing `
            -Headers @{ "User-Agent" = "Solace-FreeType-Updater/1.0" }
    }

    $actualArchiveSha256 = (Get-FileHash -LiteralPath $localArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualArchiveSha256 -ne $expectedArchiveSha256) {
        throw "FreeType archive checksum mismatch. Expected $expectedArchiveSha256, received $actualArchiveSha256."
    }
    Write-Host "Verified archive SHA-256: $actualArchiveSha256"

    $extractRoot = Join-Path $workRoot "source"
    Expand-Archive -LiteralPath $localArchive -DestinationPath $extractRoot
    $sourceRoot = Join-Path $extractRoot "freetype-$version"
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot "CMakeLists.txt"))) {
        throw "The verified archive does not contain the expected freetype-$version source tree."
    }

    $buildRoot = Join-Path $workRoot "build"
    $releaseFlags = '/O2 /Ob2 /DNDEBUG /Brepro /experimental:deterministic /pathmap:"{0}=freetype\source" /pathmap:"{1}=freetype\build"' -f $sourceRoot, $buildRoot
    $configureArguments = @(
        "-S", $sourceRoot,
        "-B", $buildRoot,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
        "-DCMAKE_C_FLAGS_RELEASE=$releaseFlags",
        "-DCMAKE_STATIC_LINKER_FLAGS_RELEASE=/Brepro",
        "-DFT_DISABLE_ZLIB=TRUE",
        "-DFT_DISABLE_BZIP2=TRUE",
        "-DFT_DISABLE_PNG=TRUE",
        "-DFT_DISABLE_HARFBUZZ=TRUE",
        "-DFT_DISABLE_BROTLI=TRUE",
        "-DSKIP_INSTALL_ALL=TRUE"
    )
    Invoke-Checked "Configuring FreeType $version (MSVC x64 static /MT)..." $cmake $configureArguments
    Invoke-Checked "Building FreeType $version (Release)..." $cmake @(
        "--build", $buildRoot,
        "--config", "Release",
        "--target", "freetype",
        "--parallel"
    )

    $cmakeLibrary = Join-Path $buildRoot "Release\freetype.lib"
    if (-not (Test-Path -LiteralPath $cmakeLibrary)) {
        throw "The FreeType static library was not produced: $cmakeLibrary"
    }

    # CMake's archive includes an MSVC-generated version resource that embeds
    # cvtres temporary paths. Repack only the compiled library objects, using
    # stable relative member names. A static archive needs no version resource.
    $objectRoot = Join-Path $buildRoot "freetype.dir\Release"
    $objects = Get-ChildItem -LiteralPath $objectRoot -Filter "*.obj" -File |
        Sort-Object Name
    if (-not $objects) {
        throw "No FreeType object files were produced under $objectRoot."
    }

    $sanitizedRoot = Join-Path $workRoot "sanitized"
    New-Item -ItemType Directory -Path $sanitizedRoot | Out-Null
    $builtLibrary = Join-Path $sanitizedRoot "freetype.lib"
    $relativeObjects = $objects | ForEach-Object {
        $_.FullName.Substring($buildRoot.Length + 1)
    }
    Push-Location $buildRoot
    try {
        Invoke-Checked "Repacking FreeType without local build metadata..." $libTool `
            (@("/nologo", "/machine:x64", "/Brepro", "/out:$builtLibrary") + $relativeObjects)
    }
    finally {
        Pop-Location
    }

    $forbiddenMarkers = @(
        [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile),
        $repositoryRoot,
        $workRoot,
        "C:\Users",
        "AppData",
        "FreeTypeUpdate",
        "lnk{"
    )
    Assert-NoAbsoluteBuildPaths $builtLibrary $forbiddenMarkers

    $headers = & $dumpbin /headers $builtLibrary
    if ($LASTEXITCODE -ne 0 -or -not ($headers | Select-String -SimpleMatch "8664 machine (x64)")) {
        throw "The generated FreeType library is not an x64 archive."
    }

    $directives = & $dumpbin /directives $builtLibrary
    if ($LASTEXITCODE -ne 0 -or -not ($directives | Select-String -SimpleMatch "/DEFAULTLIB:LIBCMT")) {
        throw "The generated FreeType library does not use the static /MT runtime."
    }
    if ($directives | Select-String -Pattern "/DEFAULTLIB:(LIBCMTD|MSVCRT|MSVCRTD)") {
        throw "The generated FreeType library contains an unexpected CRT dependency."
    }

    $stageRoot = Join-Path $workRoot "package"
    $stageIncludeRoot = Join-Path $stageRoot "include"
    $stageFreeTypeIncludeRoot = Join-Path $stageIncludeRoot "freetype"
    $stageConfigRoot = Join-Path $stageFreeTypeIncludeRoot "config"
    New-Item -ItemType Directory -Path $stageConfigRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot "include\ft2build.h") -Destination $stageIncludeRoot
    Get-ChildItem -LiteralPath (Join-Path $sourceRoot "include\freetype") -Filter "*.h" -File |
        Copy-Item -Destination $stageFreeTypeIncludeRoot
    Get-ChildItem -LiteralPath (Join-Path $sourceRoot "include\freetype\config") -Filter "*.h" -File |
        Copy-Item -Destination $stageConfigRoot

    $configuredHeaders = Join-Path $buildRoot "include\freetype\config"
    Copy-Item -LiteralPath (Join-Path $configuredHeaders "ftconfig.h") `
        -Destination (Join-Path $stageRoot "include\freetype\config\ftconfig.h") -Force
    Copy-Item -LiteralPath (Join-Path $configuredHeaders "ftoption.h") `
        -Destination (Join-Path $stageRoot "include\freetype\config\ftoption.h") -Force

    $libraryDirectory = Join-Path $stageRoot "win64"
    New-Item -ItemType Directory -Path $libraryDirectory | Out-Null
    Copy-Item -LiteralPath $builtLibrary -Destination (Join-Path $libraryDirectory "freetype.lib")
    Copy-Item -LiteralPath (Join-Path $sourceRoot "LICENSE.TXT") -Destination $stageRoot
    Copy-Item -LiteralPath (Join-Path $sourceRoot "docs\FTL.TXT") -Destination $stageRoot
    Copy-Item -LiteralPath (Join-Path $sourceRoot "docs\GPLv2.TXT") -Destination $stageRoot
    Copy-Item -LiteralPath (Join-Path $sourceRoot "src\bdf\README") -Destination (Join-Path $stageRoot "BDF-README.txt")
    Copy-Item -LiteralPath (Join-Path $sourceRoot "src\pcf\README") -Destination (Join-Path $stageRoot "PCF-README.txt")

    $harfBuzzSource = Get-Content -LiteralPath (Join-Path $sourceRoot "src\autofit\ft-hb-ft.c") -Raw -Encoding UTF8
    $harfBuzzNotice = [regex]::Match($harfBuzzSource, '(?s)\A/\*.*?\*/')
    if (-not $harfBuzzNotice.Success) {
        throw "The incorporated HarfBuzz license notice was not found in the official source."
    }
    Write-Utf8File (Join-Path $stageRoot "HARFBUZZ-LICENSE.txt") ($harfBuzzNotice.Value + "`n")

    $versionHeader = Get-Content -LiteralPath (Join-Path $stageRoot "include\freetype\freetype.h") -Raw
    if ($versionHeader -notmatch "#define\s+FREETYPE_MAJOR\s+2" -or
        $versionHeader -notmatch "#define\s+FREETYPE_MINOR\s+14" -or
        $versionHeader -notmatch "#define\s+FREETYPE_PATCH\s+3") {
        throw "The staged public headers do not report FreeType $version."
    }

    $librarySha256 = (Get-FileHash -LiteralPath (Join-Path $libraryDirectory "freetype.lib") -Algorithm SHA256).Hash.ToLowerInvariant()
    $generatedProject = Join-Path $buildRoot "freetype.vcxproj"
    $compilerVersion = Read-CMakeCompilerVersion $buildRoot
    $platformToolset = Read-MSBuildProperty $generatedProject "PlatformToolset"
    $windowsSdkVersion = Read-MSBuildProperty $generatedProject "WindowsTargetPlatformVersion"
    $cmakeVersion = (& $cmake --version | Select-Object -First 1) -replace "^cmake version\s+", ""
    $sourceMetadata = @'
# FreeType vendor package

- Version: {0}
- Official release: {1}
- Source archive: {2}
- Archive SHA-256: `{3}`
- Vendored library SHA-256: `{4}`
- License: FreeType License or GNU GPL version 2. Complete texts and the incorporated BDF, PCF, and HarfBuzz notices are in this directory.

## Build contract

- Visual Studio generator: Visual Studio 17 2022
- Architecture: x64
- Library type: static
- Configuration: Release
- MSVC runtime: `/MT` (`MultiThreaded`)
- Optional integrations: zlib, bzip2, libpng, HarfBuzz, and Brotli disabled
- Reproducibility: `/Brepro`, normalized source/build paths, and resource-free archive repacking
- CMake: {5}
- MSVC compiler: {6}
- MSVC platform toolset: {7}
- Windows SDK: {8}

The disabled integrations keep the archive self-contained. Its only MSVC default-library directives are `LIBCMT` and `OLDNAMES`. Local source, build, workspace, user-profile, and temporary paths are rejected before installation. Solace's Debug configuration continues to select `/MTd` at the final link while ignoring the archive's `LIBCMT` directive.

## Rebuild or verify

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\update-freetype.ps1
```

For an offline rebuild, download the exact archive above and pass `-ArchivePath`. The updater verifies the pinned SHA-256 before extracting or changing the vendored package, builds in a temporary directory, validates x64 and `/MT`, then replaces this directory.
'@ -f $version, $releaseUrl, $archiveUrl, $expectedArchiveSha256, $librarySha256, $cmakeVersion, $compilerVersion, $platformToolset, $windowsSdkVersion
    Write-Utf8File (Join-Path $stageRoot "SOURCE.md") ($sourceMetadata.TrimStart() + "`n")

    $incomingRoot = Join-Path $thirdPartyRoot (".freetype-update-" + [guid]::NewGuid().ToString("N"))
    $backupRoot = Join-Path $thirdPartyRoot (".freetype-backup-" + [guid]::NewGuid().ToString("N"))
    Copy-Item -LiteralPath $stageRoot -Destination $incomingRoot -Recurse

    try {
        if (Test-Path -LiteralPath $vendorRoot) {
            Move-Item -LiteralPath $vendorRoot -Destination $backupRoot
        }
        Move-Item -LiteralPath $incomingRoot -Destination $vendorRoot
        if (Test-Path -LiteralPath $backupRoot) {
            Remove-Item -LiteralPath $backupRoot -Recurse -Force
        }
    }
    catch {
        if (-not (Test-Path -LiteralPath $vendorRoot) -and (Test-Path -LiteralPath $backupRoot)) {
            Move-Item -LiteralPath $backupRoot -Destination $vendorRoot
        }
        throw
    }

    Write-Host "FreeType $version is vendored at $vendorRoot"
    Write-Host "Library SHA-256: $librarySha256"
}
finally {
    foreach ($transientPath in @($incomingRoot, $backupRoot)) {
        if ($transientPath -and (Test-Path -LiteralPath $transientPath)) {
            Remove-Item -LiteralPath $transientPath -Recurse -Force
        }
    }

    if ($KeepWorkDirectory) {
        Write-Host "Kept work directory: $workRoot"
    }
    elseif (Test-Path -LiteralPath $workRoot) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
