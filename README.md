# Vanta / Deadlock Overlay

Windows x64 external overlay. Insert opens/closes the menu and transfers input; drag the header to move it. Closing the console shuts down the reader, rendering and input worker. Alt-Tab hides the overlay. Minimizing the console keeps it running.

## Build

Use Visual Studio with Desktop development with C++, a Windows SDK and CMake 3.24 or later. Build Release directly from PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target DeadlockOverlay --parallel
.\build\Release\DeadlockOverlay.exe
```

Rebuilding the packaged updater also requires the .NET 10 SDK:

```powershell
.\tools\build-data-tools.ps1 -DotnetPath "C:\path\to\dotnet.exe"
```

The ordinary build does not run the legacy self-test, CTest, renderer or stress suites. `Solace.sln` and the older scripts under `scripts/` belong to the original GUI demo.

## Menu

- **Camera Focus** retains Fixed, V1 and Adaptive, independent X1/X2 speeds and the optional X2 360-degree single-target lock. **Tracking / abilities** contains the opt-in expanded camera controls. Focus Area offers Crosshair angle, Target Zone, pixel circle and pixel square.
- **Vindicta Ultimate** has separate **Camera**, **Auto Click** and **Auto Select** sub-tabs. Auto Select independently selects, scopes, aims and fires at visible enemy heroes below 49% health, using a live damage estimate and an acquisition limit of 60% charge-ring fill. It starts disabled. Assassinate retains zero projectile-flight lead. See [Auto Select behavior and validation](docs/VINDICTA_AUTO_SELECT.md).
- **Visuals** keeps Health bars and Skeletons / Hitboxes, and adds Hero tags (icons/names and distance), World and Minimap sections. The new world renderer uses fresh positions and local game icons. World and minimap enablement are independent; the minimap shows all detected enemies.
- A textured Vindicta **visual preview** stays beside the menu across pages, using the same player drawing functions. Rotate it or change its sample health/occlusion; toggle the window in Appearance. See [hero preview and shared Solace controls](docs/VISUAL_PREVIEW.md).
- **Auto Click** retains current-hitbox and soul-orb claimability checks. **Auto Parry** retains melee/Counterspell handling and optional Puddle Punch detection. Scripts, spin and Active Reload were removed.
- **Configurations** manages named presets. Explicit and remembered named-preset loading turns automation off while restoring preferences.
- **Connection** manages maps, schema and validated reader profiles. **Diagnostics** retains the session monitor. **HUDs** contains Mid Boss, Rift, Parry, Session, Spectators and custom-crosshair settings; Rift's **!** tooltip marks it as in testing. **Appearance** retains the rendering cap, frame transparency, glass cursor and preview-window setting.

Existing configuration values are preserved; gameplay functionality is opt-in. The menu's visual preview starts enabled. See [external features and behavior](docs/EXTERNAL_FEATURES.md) for controls, input cancellation, presets and diagnostics. See [icon assets](docs/ICON_ASSETS.md) and [world/minimap behavior](docs/WORLD_OVERLAY_RECODE.md).

## Game updates

Start Deadlock, then use **Connection → Update all data**. Load sandbox, bots or a replay with a remote living player so the updater can validate the new reader against live data. **Install all maps** exports collision meshes from your own installation. Both buttons also refresh the local hero/NPC/minimap asset catalog and hero preview model. **Dump schema** can run separately.

Public fields resolve from the schema dump. Globals and private layouts require verified code witnesses; incompatible patches report the missing capability or require a resolver update. Failed reader candidates leave the working profile installed. The external runtime does not import Base's DLL injection, silent aim, related recoil logic or material chams.

Generated maps, dumps, profiles, binaries, local settings and verification reports are excluded from Git. Rebuild the updater whenever its reader contract or resolver resources change. See [game data updates](docs/GAME_DATA_UPDATES.md), [MAKCU input](docs/MAKCU_INPUT.md), [hitboxes](docs/HITBOXES.md) and [historical notes](docs/README_HISTORY.md).
