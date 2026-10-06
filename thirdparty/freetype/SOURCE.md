# FreeType vendor package

- Version: 2.14.3
- Official release: https://sourceforge.net/projects/freetype/files/freetype2/2.14.3/
- Source archive: https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/ft2143.zip
- Archive SHA-256: `566518a6d5a5cfbfc9697fca5b59571de5a357f9d6fe41c7cf8adc80c4b0bd06`
- Vendored library SHA-256: `ab34e653da2bc271ba2f546223973d985eab0d0fe3dafee5ada6c5a857dc7c6d`
- License: FreeType License or GNU GPL version 2. Complete texts and the incorporated BDF, PCF, and HarfBuzz notices are in this directory.

## Build contract

- Visual Studio generator: Visual Studio 17 2022
- Architecture: x64
- Library type: static
- Configuration: Release
- MSVC runtime: `/MT` (`MultiThreaded`)
- Optional integrations: zlib, bzip2, libpng, HarfBuzz, and Brotli disabled
- Reproducibility: `/Brepro`, normalized source/build paths, and resource-free archive repacking
- CMake: 4.3.4
- MSVC compiler: 19.44.35226.0
- MSVC platform toolset: v143
- Windows SDK: 10.0.26100.0

The disabled integrations keep the archive self-contained. Its only MSVC default-library directives are `LIBCMT` and `OLDNAMES`. Local source, build, workspace, user-profile, and temporary paths are rejected before installation. Solace's Debug configuration continues to select `/MTd` at the final link while ignoring the archive's `LIBCMT` directive.

## Rebuild or verify

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\update-freetype.ps1
```

For an offline rebuild, download the exact archive above and pass `-ArchivePath`. The updater verifies the pinned SHA-256 before extracting or changing the vendored package, builds in a temporary directory, validates x64 and `/MT`, then replaces this directory.
