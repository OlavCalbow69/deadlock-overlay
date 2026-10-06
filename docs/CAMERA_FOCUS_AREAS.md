# Camera focus areas

Open the menu with Insert, select Camera Focus, then press **Focus area**. Choose a mode and adjust its radius slider. **Show FOV** on the main camera page, or **Draw this area** in the popup, draws the current mode. Drawing is independent of the older Focus visuals setting and does not enable steering. Each mode stores its own drawing toggle; the two target modes share their meter-radius setting.

| Mode | Slider | When the target qualifies |
| --- | --- | --- |
| Center FOV | 1–80% of the shorter screen dimension | Its aim point is inside the circle around the screen center. The default 40% preserves the previous selection radius. |
| Target box (3D) | 0.1–50 meters | The forward ray through the screen center intersects the world-aligned box around the selected Head, Body or Pelvis bone. |
| Target circle (2D) | 0.1–50 meters | The crosshair is inside a screen circle centered on the selected bone. Its radius represents the specified world distance in a camera-facing plane at the target's depth. |

A 10 m box extends 10 m in each of the six directions from its center, so each side is 20 m long. It is a real 3D volume, not the rectangular screen bounding box of its projected corners. A 10 m target circle has a 10 m radius, becomes smaller as the target moves away, and remains circular on screen. Areas are centered on the current bone position; prediction affects the eventual focus point, not the area's center.

Hold either mouse side button with Camera focus enabled, Makcu connected, Deadlock in the foreground and the menu closed. The entity, selected bone, enabled target type, team filter and visibility criteria must be valid. Camera steering requires a snapshot at most 50 ms old. There is no separate maximum world-distance setting. The existing mode gate currently returns true for all game modes; the Sandbox & bots switch does not restrict this gate.

If nothing qualifies when you press, keep holding and move the crosshair into an area. Acquisition retries on fresh snapshots. A chosen target stays stable while it remains eligible. Leaving its area or losing its full entity handle stops that selection and permits acquiring another eligible target. An occluded retained target pauses steering; opening the menu, switching away from the game or releasing the button releases the selection. A failed input submission requires releasing before retrying.

Candidates are ranked by distance from the screen center to their current aim point. With Free movement enabled, that is the nearest point on the head–body–pelvis line; target areas still use the chosen bone as their center. Distances are compared in 0.001-pixel bins to avoid floating-point noise in symmetric positions. On a distance tie, the lowest current HP wins. Unknown health is ranked after known health. If distance and health are both tied, reservoir sampling chooses uniformly among the tied candidates. Random selection happens on acquisition, avoiding frame-to-frame switching while locked. All enabled types, including minions and soul orbs, participate in this rule.

The box drawing clips each of its 12 edges against the camera plane and viewport before projecting it. The circle drawing uses fixed segment counts and omits boundaries wholly outside the viewport. Rendering uses the sampler's cached visibility results; actual steering still checks the exact focus point against the collision mesh. Selected target areas are gold and other eligible-type areas are blue. Normal foreground and freshness rules apply even when all other HUD drawing is disabled.

The meter conversion is `world_units * 0.0254`. It was checked against the installed client.dll, SHA256 `f66a0fdfe60029cca711dff32644941303396c2d9d16774a6cb2570c28520b10`: the function at RVA `0xF2B890` computes Lash down-strike height from `m_flStartHeight` (`+0x1F5C`) and multiplies by the shared `0.0254f` constant at RVA `0x2511378`. The focus-area implementation converts meters back to world units with that factor.

Settings persist in `%LOCALAPPDATA%\DeadlockOverlay\settings.ini`: `focus_area_mode` (0, 1, 2), `focus_fov_percent`, `focus_radius_meters`, and `show_focus_area_0` through `show_focus_area_2`. Values are validated when loaded.

Verification includes metric sizes, depth scaling, rotated cameras, boundaries, behind-camera rejection, edge clipping, lower-health and randomized ties, stable held selection, reacquisition, configuration persistence, renderer previews for all modes, console shutdown and resource checks with all three drawing modes exercised.
