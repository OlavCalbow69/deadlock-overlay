# Deadlock Overlay

Build with `./build.ps1`, then run `build/Release/DeadlockOverlay.exe`. Insert opens/closes the menu and transfers input to it. Drag the header to move the menu. Closing the console stops the program. Alt-Tab hides the overlay; minimizing the console does not suspend it.

## Build from source

Requires Windows x64, Visual Studio 2022 with **Desktop development with C++** and a Windows SDK, CMake 3.24 or newer, and the .NET 10 SDK. Run these commands in PowerShell:

```powershell
git clone https://github.com/OlavCalbow69/deadlock-overlay.git
cd deadlock-overlay
.\build.ps1
.\build\Release\DeadlockOverlay.exe
```

If `dotnet` is not on PATH, use `.\build.ps1 -DotnetPath "C:\path\to\dotnet.exe"`. The first build restores the updater's NuGet packages and builds the pinned schema dumper. Later builds reuse these tools; use `-RebuildDataTools` after changing their source.

After launching, open **Connection** and press **Install all maps** to export collision meshes from your own Deadlock installation. **Update all data** also creates a schema dump from the running game. Generated maps, schema dumps, executables, diagnostic reports and local settings are excluded from Git. The source and required vendored build dependencies are included.

The root `build.ps1` builds the overlay. `Solace.sln` and the older scripts under `scripts/` belong to the original GUI demo.

## Menu

- **Camera Focus**: first sidebar tab, with the Phosphor crosshair-simple icon. Camera focus, focus visuals, sandbox/bot testing, Head/Body/Pelvis, speed, Free movement and prediction are together here. **Focus area** selects Center FOV, Target box (3D), or Target circle (2D), with a radius slider. **Show FOV** draws the active area independently of Focus visuals; each mode remembers its drawing setting. Target types selects players, minions and soul orbs independently, and includes the shared enemies-only and visible-only filters. Players remain the default. See [focus areas](docs/CAMERA_FOCUS_AREAS.md).
- **Health bars**: horizontal bars, numbers, enemies-only and visible-only filters, head/body/pelvis markers, skeletons and a live preview. Width, height, head gap and health colors remain on this page. Vertical bars stay removed.
- **Skeleton colors**: the preview card has Visible and Blocked swatches. Segments are blue when clear, purple when blocked, and gray when visibility cannot be checked. Skeletons show both states even with Visible only enabled; that filter still gates bars, dots and focus.
- **Connection**: refresh/FPS/read timing, map visibility, camera projection and Makcu connection details. **Update all data** exports new/changed installed maps and creates a fresh schema/SDK dump with one click. **Install all maps** and **Dump schema** can also run separately; progress, cancellation and reports are shown here. See [game data updates](docs/GAME_DATA_UPDATES.md).
- **Appearance**: the sparkles button beside the header's close and theme buttons opens Transparent frame and its 40–90% opacity slider. Transparency defaults on and affects the sidebar, header, footer and empty frame; settings cards and the active tab stay solid. Turning it off restores the solid dark frame. The toggle and opacity persist with the other settings. This uses the overlay's existing transparency support, without a desktop blur pass.
- **Glass cursor**: the Windows arrow stays hidden inside the focused menu while the glass cursor is active; disabling/unavailability of the effect restores the arrow. The switch is available on Health bars and in Appearance. Original fonts, rounded panels and theme animation remain.
- Reset defaults and Close menu remain on the health-bar page.

The menu uses the original [Solace ImGui shell](https://github.com/poncippg-spec/Free-Solace-ImGui-Interface) with the added overlay controls. Original copyright and third-party notices are preserved.

## Input and visibility

Hold either side button to focus the nearest enabled target inside the selected area. Equal screen distances prefer lower current health, then a random choice. The full entity handle stays selected while it remains eligible; an empty acquisition, disappearance or leaving the area allows another acquisition while the button remains held. Makcu connects through the CH343 management serial port. Menu input, Alt-Tab, release, invalid/stale data and occlusion stop steering. The current `camera_mode_allowed` gate accepts all modes; the sandbox toggle does not impose a mode restriction.

Free movement uses the nearest point on the projected head–body–pelvis polyline. Moving along it is free; moving away produces a correction to its nearest point, including the head/pelvis endpoints. Perspective interpolation recovers the world point for visibility and prediction. This requires no physical-axis telemetry and avoids counting Makcu's own corrections as manual movement. Soul orbs use their center. Minions use named bones when available, otherwise their transformed collision center. See [Makcu backend](docs/MAKCU_INPUT.md).

Visible only defaults on. Static map collision segments determine which players/markers and exact focus points are clear. Moving/destructible obstacles and rendered opacity may differ. See [visibility details](docs/VISIBILITY_EXPLAINED.md).

Live matches can report `start` as their bootstrap level. Visibility resolves the actual loaded arena through `worldrenderer.dll` before selecting its collision mesh.

Settings persist in `%LOCALAPPDATA%/DeadlockOverlay/settings.ini`. The external reader supports build **6753**, client.dll SHA256 `678aec94adb44623e335ee7ec76c08f4477a0ea88a85cea5cb70abace1bacaa8`, and uses ReadProcessMemory. Use windowed/borderless mode. See the [6753 profile update](docs/BUILD_6753.md).

The reader uses validated binary profiles. A new game patch may require a source update even after maps and schema have been refreshed.

## Verification

`build.ps1` builds Release, copies map files beside the executable and runs logic, renderer, console-shutdown and GUI resource checks. `--makcu-probe` checks the serial connection without movement. `--probe` captures skeleton and additional target data without steering. `--benchmark --extended-read` measures all these reads enabled. Normal `--diagnostics` writes runtime reports, including target kind in `camera.json` and extra targets/skeleton counts in `session.json`.

The packaged data updater also has `--self-test` checks for map fingerprints, interrupted publication recovery, incomplete dumps and binary/layout compatibility gates. Rebuild it with `build.ps1 -RebuildDataTools -DotnetPath <dotnet.exe>` after modifying the tools. Fresh dumps do not bypass the reader's supported-binary checks; a patch that changes code addresses can still need a profile update.

Historical research is retained in the documentation and [previous README](docs/README_HISTORY.md); older feature-status statements do not override this README. Local verification reports are kept outside the tracked source.
