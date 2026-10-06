# Deadlock Overlay

Run DeadlockOverlay.exe. Insert opens/closes the menu and transfers input to it. Drag the header to move it. Closing the console stops the reader and destroys both overlay windows.

- **Visible only** filters drawing and camera focus using the current map collision mesh. It is on by default. Fully blocked players are hidden; focus pauses when its exact point is blocked. See VISIBILITY.md for map coverage and limitations.
- **Enemies only** hides players on your local team. It is off by default and persists with other settings. If your team is unavailable, drawing remains enabled for all teams.
- **Vertical bar** places a bottom-up healthbar beside the torso. Width/Height/Head gap become Length/Thickness/Side gap. Horizontal mode stays above the head.
- **Head dot**, **Body dot** and **Pelvis dot** are independent switches. They follow named head, spine_2 (or chest) and pelvis bones. Missing/invalid/offscreen positions are skipped. Markers can remain enabled with healthbars turned off, and follow the Enemies only filter.
- Marker colors: white head, cyan body, amber pelvis, each with a dark outline. New options default off and persist in settings.
- **Health numbers** displays current health in a larger semibold font. The bar still represents current / maximum health.
- Menu labels use larger semibold fonts and short descriptions. The original Solace shell, icons, shaders and animated controls remain integrated.
- Healthbars stay click-through and continue beneath the menu. Alt-Tab hides the overlay. Minimizing the console does not suspend it.
- Settings: `%LOCALAPPDATA%\DeadlockOverlay\settings.ini`.

This build supports client.dll SHA256 `948260612c9b7243964e4a0d5f0f6252ae846ceaa6e8c0e95b83bd5eba4caf60` (user's build 6745). Unsupported DLLs disable drawing. It uses external ReadProcessMemory calls; no kernel driver or mapper is installed. Use windowed/borderless mode. Hidden-player data availability is unverified.

## Performance and validation

Rendering and sampling target the game monitor's refresh rate (240 Hz on this machine). Actual FPS depends on load. The current audit recorded 218 FPS during the active samples. Window discovery is cached instead of enumerating all windows every frame, memory reads are batched, and RTTI/model caches have explicit size limits. Font/animation caches are cleared when the GUI is destroyed.

Experimental DXGI frame tracing is now opt-in with `--game-frames`. It delivered events too late to provide reliable frame synchronization in the live probe; normal operation avoids its overhead and uses refresh pacing. It observes submissions, not completed GPU frames or monitor scanout. Delayed/absent events fall back to timer pacing.

All four test suites passed: 380 logic checks, transparency/shader/render checks, console shutdown, and a resource stress test with 6,600 frames over three full GUI rebuilds. The stress test switches cursor effects and repeatedly shows/hides the GUI. Handle/window-object counts remained stable. A 45-second runtime sample had stable memory use. These checks found no sustained leak evidence; they are not proof of long-term leak freedom. See RESOURCE_AUDIT.md and the JSON reports for measurements and limitations.

## Diagnostics

```
DeadlockOverlay.exe --diagnostics
DeadlockOverlay.exe --probe
DeadlockOverlay.exe --benchmark
DeadlockOverlay.exe --frame-trace-probe
DeadlockOverlay.exe --game-frames --diagnostics
DeadlockOverlay.exe --visibility-test
DeadlockOverlay.exe --self-test
DeadlockOverlay.exe --render-test
DeadlockOverlay.exe --shutdown-test
DeadlockOverlay.exe --resource-test
```

Reports are written beside the EXE. Probe, benchmark and test modes exit after their checks. Resource tests briefly display test windows. The diagnostics page displays actual FPS and read time.

## Source

`source/` and `source.zip` contain the complete CMake project with vendored dependencies. Rebuild with CMake and Visual Studio Desktop development with C++ using `source\build.ps1`. The Desktop copy has the project directly in its root; run `build.ps1` there.

UI assets and effects: [Free Solace ImGui Interface](https://github.com/poncippg-spec/Free-Solace-ImGui-Interface), commit `6b88e2ed65b06ffbfa26b1aa4561579707fbeb4f`. Preserve the included licenses when redistributing.


The marker update was verified against two live player models with distinct head/torso/pelvis positions. The warm reader benchmark averaged 53.633 microseconds and 34 memory-read calls per sample on this machine. Marker positions share a batched transform read. Render previews: marker-settings-preview.png and markers-preview.png. All four suites passed; after adding preview clipping, logic/render/console checks were repeated and passed.


## Presentation and priority update

The default renderer now uses DirectComposition with a one-frame queue and waits before sampling/drawing. The nonblocking frame-dropping trial has been removed. Startup requests High process priority; the diagnostics page shows the actual class. Use --normal-priority for comparison or --legacy-renderer for the previous renderer. frame-performance.json includes queue_wait_ms and priority_class. The FPS counter counts successful presentation submissions; it does not measure monitor scanout. See PERFORMANCE_FIX.md for results and limitations.

## Camera focus

Hold either side mouse button to focus the player nearest screen center. Connection has Camera focus, a Body/Head/Pelvis button, and Sandbox & bots debug mode. Body is the default; replay and detected sandbox/CoopBot modes are supported. Release stops steering; opening the menu or Alt-Tab also stops it. The feature uses Windows mouse input and rotates the camera without moving its position. See CAMERA_FOCUS.md for controls, verified mode detection and testing limits.

Focus speed is now adjustable from 1 to 120 in Connection. The new default is 12, with a raised turn-rate limit. Lower it if tracking overshoots.


Connection now includes Free movement (vertical mouse input along the connected head/body/pelvis line) and opt-in projectile Prediction. Projectile speed is configured manually in world units/s, not automatically read from the weapon. See CAMERA_FOCUS.md for controls, movement inheritance, assumptions and validation.


Automatic primary-weapon speed now defaults ON in Connection. It detects the current local weapon and item-adjusted bullet velocity, updates after hero changes, and reads movement inheritance from weapon data. Turning it OFF restores the manual slider. Invalid/stale data or unsupported special-shot profiles pause prediction; see CAMERA_FOCUS.md for scope and checks.


## Focus visuals and faster response

Connection → **Focus visuals** independently controls the gold focus ring, the connected head/body/pelvis line and bone dots automatically forced on by Free movement. Switching it off leaves camera focus, free movement and prediction active. Independently enabled Head/Body/Pelvis dot settings and healthbars still follow their own switches. The preference persists as `focus_visuals` (default on).

Focus speed now ranges from 1 to 120. The existing local setting was increased from 30.0209 to 60 for this user's requested faster response; other preferences were retained. The new-install default remains 12. Correction is `screen_error * min(speed * min(dt, 0.025), 0.85)`, with persistent fractional counts and the existing speed-dependent turn-rate limit. At ordinary speeds the previous response is retained; at high speeds a single correction is bounded to reduce large-step overshoot. This is not calibrated to the game's mouse sensitivity; lower the speed if it oscillates.

Foreground/menu/side-button checks and steering now occur before the GPU queue wait. A saturated visible queue waits at most four milliseconds per poll instead of fifty before another controller update, without preparing/dropping a render frame. Drawing refreshes its snapshot after that wait. The controller still shares the main loop with rendering, so this does not guarantee a fully independent input cadence or eliminate stalls inside other graphics calls. All visibility/mode/staleness gates remain in force.

384 logic checks, including faster high-speed response and bounded positive/negative corrections, passed. Rendering/transparency and console shutdown checks passed; the new GUI control was visually inspected. Before this change the live reader measured 0.2331 ms and camera diagnostics recorded zero SendInput failures. These establish neither kernel-driver speedups nor game-side input-consumption latency. No kernel driver or hardware input transport was added. Deadlock was closed during final deployment, so the faster response has not yet been confirmed in-game.
