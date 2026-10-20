# Adaptive camera focus

Choose **Camera Focus -> Movement -> Adaptive**. Vindicta Ultimate has independent focus settings. Existing saved modes remain unchanged until Adaptive is selected.

## Mouse response

Calibration is not required or started by the application. The setup buttons, saved-view checklist and calibration warnings have been removed from both focus profiles. Existing `adaptive-calibrations.ini` records are preserved on disk but are not read or used by this build.

Adaptive uses visual feedback through the current render matrix. Each fresh camera snapshot supplies the permitted focus point and projection, including weapon zoom, Assassinate, slides, ability FOV changes and custom aspect ratios. A bounded angular correction is projected to screen displacement and submitted through the existing MAKCU `km.move` transport. There is no saved-FOV matching gate.

The screen response uses the legacy focus convention of one projected pixel per mouse count. Speed controls its convergence; the actual camera response still depends on game sensitivity, mouse settings and resolution. The configured step-rate and acceleration limits bound requested corrections, not a calibrated physical turn rate. Adjust Speed for the desired feel. Invalid or stale camera data still stops movement.

## Areas and permitted movement

**Focus Area -> Area** offers **Crosshair angle** and **Target Zone**. Crosshair angle measures angular distance to the nearest permitted geometry through the actual render matrix, including custom aspect ratio and zoom. Radius defaults to 6 degrees (1-25); retention is 1.3 times that radius.

These area choices are also available with Fixed bone and V1; changing area
does not switch the movement controller. Target Zone uses the selected bone's
region for Fixed bone, head/torso for V1, and the
chosen Free region for Adaptive. Activation/retention use the same cached
expanded primitives as the outline. Former screen-percent and metric 2D/3D
area settings migrate once; new saves mark focus_area_version=2 separately for
each profile.

Target Zone follows actual current hitboxes. Padding defaults to 12% of full active body height (0-100%). Height is smoothed over 150 ms and resets when the model or active hitbox set changes. Spheres/capsules expand their radii; boxes expand along their own axes. Acquisition intersects this union with the crosshair ray. Retention uses 1.3 times the padding. Missing geometry/scale excludes that target with a diagnostic reason.

The original, unpadded permitted hitboxes remain the free region. Choose **Head and torso** or **Whole body**. Head/torso excludes arms and legs; unknown regions are excluded from restricted choices and included in Whole body. Classification uses each model rig's bone names and hierarchy. Minions retain body geometry; soul orbs retain center focus and validated collision-bound activation sizes. The removed Selected bone region setting migrates to Head and torso; the removed V2 movement setting migrates to Adaptive/Whole body. Both camera profiles migrate independently.

Inside permitted geometry, no correction is submitted and accumulated motion clears. Outside it, focus returns to the closest valid point, inset 5% toward that hitbox's center. The previous hitbox wins if within 0.1 degrees of the best alternative; preferred bone only breaks near-ties. Gaps between hitboxes remain gaps.

Current targets are retained while valid. Acquisition ranks nearest angular correction, then lower health, then a random final tie break. Target death or retention loss clears motion and starts a 100 ms reacquisition delay while activation remains held. Visible only checks current, predicted and inset points. With no visible permitted point, movement stops immediately; the target is reserved for at most 100 ms.

The optional X1 / X2 lock (360 deg) activation overrides those area/reacquisition
rules for X2 only. It selects one eligible full entity handle across 360 degrees
and keeps it until lost or released, using the existing X2 response speed and
permitted region. Offscreen/rear points use finite spherical angular errors;
front-facing regions retain the precise hitbox solution. Losing the target or
its valid visible point ends that hold, with no replacement until X2 is released
and pressed again. X1 retains the ordinary Adaptive behavior described above.

**Show zone** draws only the nearest eligible preview, or the retained target. The activation union is blue, selected gold, with faint retention edges. Internal overlaps are suppressed. Projection uses the same expanded volumes and actual render matrix, clips at camera/viewport boundaries and approximates curves within 0.5 pixels. Padding never enlarges free movement or authorizes auto click.

## Response and prediction

Speed 1-100 maps logarithmically from a 250 ms to 20 ms requested response time (default 70). Elapsed time and persistent fractional mouse counts provide frame-independent exponential convergence. Advanced can lower the 720 degrees/second requested step rate and 6,000 degrees/second-squared acceleration limits. Requests are bounded by the remaining projected correction. Target/profile changes, cancellation and entering the free region clear movement state. The controller advances once per fresh snapshot. MAKCU transport remains `km.move`; firmware interpolation is unchanged.

Motion timestamps mark when root positions are measured. Schema `m_vecAbsVelocity` is enabled after read-only comparison against displacement; invalid readings fall back to a 16-32 ms position window with a 15 ms exponential filter. Reversals, discontinuities and sample gaps reset filtering.

Prediction separates data-age extrapolation from projectile flight time. Effective weapon speed and shooter velocity inheritance are reused; inheritance applies only to projectile travel. Measured host queue delay is included when available. Permitted geometry is translated by lead and the closest point recomputed for up to three intercept iterations. Assassinate has zero projectile-flight lead. Invalid projectile data falls back to current geometry with an explanatory status. Gravity, drag and muzzle-origin research remain outside this model.

Auto click always checks actual current hitboxes. Predicted geometry cannot authorize firing. Menu opening, Alt-Tab, minimization, stale data and disconnects stop new corrections.

## Diagnostics and validation

Advanced shows the current render FOV/aspect, response time, step limits and focus diagnostics. With `--diagnostics`, `adaptive.json` reports `response_backend: render_projection`, `calibration_required: false`, projected pixel errors, requested counts, area/retention, region, motion source, prediction age/flight, queue delay and pause reason. Target Zone also reports scale, padding, primitive counts and intersection states. `camera.json` carries the Adaptive report while that mode is selected. `--probe` is read-only and never opens MAKCU.

Build Release directly through CMake. Do not run the existing self-test, CTest or resource/stress suites. Focused projection/response checks and offscreen previews do not establish interactive motion quality. In-game checks include normal/zoom/Assassinate, ability FOV changes, slides, target loss, visibility, overlapping targets and model changes. Include Baba, Rat King and Solomon when available.
