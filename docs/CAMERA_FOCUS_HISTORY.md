# Camera focus

Hold either mouse side button to focus the nearest eligible player within the center acquisition circle. The target stays selected for that hold. Release and press again to select another player. Connection contains Camera focus, Head/Body/Pelvis, focus speed (1–120, default 12) and Sandbox & bots debugging. Debug mode respects Enemies only; replay acquisition permits both teams. Existing preferences are preserved.

Steering sends relative Windows SendInput motion, rotates the view without translating it, and never fires or writes game memory. Release, menu opening, Alt-Tab, minimizing, samples older than 50 ms, missing/offscreen bones, disabled settings and rejected mode detection stop steering. An input failure or lost target requires release and repress. Sensitivity/inversion affect response. Earlier sandbox fixed-bone focus was user-confirmed; end-to-end replay and separate CoopBot movement remain unverified.

Mode detection remains read-only and tied to the validated build. client.dll SHA256 948260612c9b7243964e4a0d5f0f6252ae846ceaa6e8c0e95b83bd5eba4caf60; engine2.dll SHA256 0782caed3e1c476389fe2a27a0713d47567a5237f706b151ce7cc34a05dbadc3. Demo playback uses engine global +0x5B92B0, vtable +0x4D7E88 and byte +0x1230. Rules use client pointer +0x3C17AE0, vtable +0x26859B0, match mode +0xA8 and game mode +0xAC. Sandbox game mode=3; CoopBot match mode=3. Testing variants also use client +0x367E830 convar value +88 in match mode 0/2 or recognized testing map names. Ordinary online modes are not enabled by the debug switch alone. Implausible class sizes in the supplied dump were treated as unverified leads.

[Windows SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput). Diagnostics camera.json records mode, target, input counts, raw-input readiness, free-line position and prediction state.


## Free movement and prediction

Connection → Free movement is enabled by default. Hold either side button and move your mouse vertically: down moves from head through body toward pelvis, up reverses it. The initial point uses your selected Head/Body/Pelvis setting. Movement interpolates the two world-space bone segments and clamps the parameter to [0, 2]. Releasing and pressing again starts at the selected bone. Both endpoints and the body bone must be valid and on screen. The connected line follows the same team filter as the dots. Turning Free movement off restores fixed-bone focus.

Raw Input observes relative hardware mouse movement without taking foreground focus or suppressing the game's input. Generated steering carries an extra-information tag; tagged/raw input without a device handle is excluded from manual movement. Precision touchpads with no device handle are not supported by this free-movement path. [Microsoft Raw Input documentation](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input).

Prediction is opt-in. Projectile speed is CONFIGURED in game world units per second, range 1,000–100,000; 30,000 is an editable starting value, not a measured weapon speed. The dump supplied here does not expose a verified live weapon-speed read. The implementation estimates target and local velocity from pelvis samples at intervals of at least 30 ms, smooths over roughly 60 ms, and resets on stale data, backwards time, large position jumps, or implausible speed. The target history is capped at 512 handles and removes absent players every sample.

Distance comes from the camera origin recovered from the projection matrix to the selected point. A quadratic solves straight-line, constant-speed projectile travel against target velocity. Inherit shooter velocity subtracts measured local velocity from the intercept calculation; enable it only for a projectile that actually inherits that motion. Shooter motion is not blindly subtracted when inheritance is off. The camera origin approximates the firing origin; the actual muzzle offset is not read. No bullet drop, drag, acceleration, network latency, spread, or automatic weapon/upgrade speed read is claimed. Solutions beyond one second, unreachable intercepts, missing velocity, and invalid matrices fall back to ordinary focus. In replay, inherited shooter velocity requires an available local pawn.

Free movement remains clamped along the head/body/pelvis line in the predicted target pose; prediction can shift the gold focus ring away from the current blue bone line. The feature rotates the camera and never fires.

Latest verification: 86 logic checks pass, including bone interpolation/clamping, travel-time roots, unreachable/invalid/long flights, camera-origin recovery and velocity reset. Renderer and console cleanup pass; camera-settings-preview.png was inspected. A resource retry passed three 2,200-frame cycles with private bytes 94,793,728 unchanged, handles 1,636 unchanged, GDI 0 unchanged and USER 11 unchanged. The first resource run had a transient increase (29 handles, 7 GDI, 9 USER, 258,048 private bytes); the unchanged test passed on retry. This is a bounded check, not proof against long-session leaks. New free movement and prediction still need an end-to-end user test in the game; the earlier fixed-bone steering was user-confirmed.

Live diagnostics after deployment reported raw_ready=true, a changing free-line position, prediction_active=true with travel time 0.00973817 s, local_velocity_valid=true and zero SendInput failures. This confirms activation and data flow; movement quality still depends on the user's in-game evaluation. See free-movement-live.json and focus-reader-live.json.



## Automatic primary-weapon speed update

Connection → Auto weapon speed now defaults to ON and persists. The read-only reader follows the local controller's current serial-validated pawn, its primary-weapon slot (21), current ability VData and the `primary` entry in its weapon-info tree. It reads the actual base bullet speed and movement-inheritance coefficient rather than a per-hero constant. Hero switches re-resolve this chain on subsequent samples; unavailable data never retains the previous hero's speed.

Bullet-velocity modifier 170 is read from the live aggregate mirror. Version, completed write sequence, result type, dirty state, context key and policy are checked; tick-sensitive policies also require the current tick. The mirror is populated even when the engine disables its cache-read optimization, so that optimization flag is not treated as mirror validity. The aggregate is read twice to reject concurrent changes. Absent modifier groups mean zero bonus; invalid/stale data means unavailable rather than zero. Controller, pawn, primary slot and VData references are rechecked before publishing.

The effective primary speed is base × (1 + bonus/100). The weapon coefficient scales local movement directly; the manual inheritance checkbox is ignored in automatic mode. GUI shows live speed, base, bonus and movement inheritance. Switching Auto weapon speed OFF restores the existing configured speed and manual inheritance option.

Ordinary primary bullets with supported modifier caching are covered. An active base-speed override (modifier 171), random-speed weapon, unsupported modifier policy, missing local pawn or invalid data pauses automatic prediction. Charged shots and ability-specific projectiles are not claimed to use the primary-gun profile. In replay without a local weapon, use manual mode. Basic camera focus remains available when automatic prediction data is unavailable.

Validation: 101 logic checks passed, including modifiers changing versions, incomplete writes, dirty/stale caches, tick freshness, context mismatch and invalid weapon values. Renderer/transparency and console cleanup tests passed, and the automatic controls preview was inspected. Live deployed diagnostics matched Vindicta with High-Velocity Rounds: base 25,984.3, bonus +60%, effective 41,574.9 units/s, inheritance 0. This update did not repeat the previously passed resource stress suite. All earlier statements that the running build only uses manual speed are superseded by this section.

Live hero-switch capture also observed distinct primary-weapon handles with base speeds 8,000, 32,600, 30,000, 62,500 and 25,000 units/s. Each published profile was valid and used its current speed; see auto-speed-live.json, auto-speed-item-live.json and auto-speed-current.json. The captures after switching heroes had zero bullet-speed bonus; the initial deployed Vindicta profile verified the +60% bonus branch. A same-hero buy/sell transition was not captured in these intervals.



## Static map visibility update

Health bars → Visible only now gates acquisition and held focus, including the interpolated bone point and predicted point. A blocked point pauses steering while retaining the selected target, and resumes when clear; missing mesh/camera data fails closed. See VISIBILITY.md for the four extracted map meshes, diagnostics, tests and static/dynamic limitations. Latest checks: 380 general logic checks and 512 real-map known-wall checks passed. After Deadlock reopened in dl_hideout, the user confirmed that blocked markers disappear and blocked camera focus stops.


## Focus visuals and faster response

Connection → **Focus visuals** independently controls the gold focus ring, the connected head/body/pelvis line and bone dots automatically forced on by Free movement. Switching it off leaves camera focus, free movement and prediction active. Independently enabled Head/Body/Pelvis dot settings and healthbars still follow their own switches. The preference persists as `focus_visuals` (default on).

Focus speed now ranges from 1 to 120. The existing local setting was increased from 30.0209 to 60 for this user's requested faster response; other preferences were retained. The new-install default remains 12. Correction is `screen_error * min(speed * min(dt, 0.025), 0.85)`, with persistent fractional counts and the existing speed-dependent turn-rate limit. At ordinary speeds the previous response is retained; at high speeds a single correction is bounded to reduce large-step overshoot. This is not calibrated to the game's mouse sensitivity; lower the speed if it oscillates.

Foreground/menu/side-button checks and steering now occur before the GPU queue wait. A saturated visible queue waits at most four milliseconds per poll instead of fifty before another controller update, without preparing/dropping a render frame. Drawing refreshes its snapshot after that wait. The controller still shares the main loop with rendering, so this does not guarantee a fully independent input cadence or eliminate stalls inside other graphics calls. All visibility/mode/staleness gates remain in force.

384 logic checks, including faster high-speed response and bounded positive/negative corrections, passed. Rendering/transparency and console shutdown checks passed; the new GUI control was visually inspected. Before this change the live reader measured 0.2331 ms and camera diagnostics recorded zero SendInput failures. These establish neither kernel-driver speedups nor game-side input-consumption latency. No kernel driver or hardware input transport was added. Deadlock was closed during final deployment, so the faster response has not yet been confirmed in-game.
