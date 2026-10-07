# Free movement

Open **Insert → Camera Focus → Free movement** and choose one mode:

| Choice | Behavior while the selected Hold button is held |
| --- | --- |
| Off | Focus the selected Head, Body or Pelvis bone. |
| V1: Bone line | Keep the original nearest-point rule on the head–body–pelvis line. Movement along the line is free; correction brings the crosshair back to its closest point. |
| V2: Full hitboxes | Move freely anywhere within the projected union of the target's animated hitboxes, including arms and legs. Outside that union, correct toward the closest point in a hitbox. |

**Camera Focus → Hold button** chooses **Either side button** or **Left mouse** for all three modes. The selected button must stay held, and releasing it cancels queued corrections. Existing configurations default to side buttons. The choice persists as `focus_hold_button` (`0` = side buttons, `1` = left mouse), and camera/Makcu diagnostics report it as `hold_button`.

V2 uses the full capsule, sphere and oriented box volumes, independently of the wireframes used for drawing. Gaps between hitboxes remain gaps. A ray through any volume produces no correction or accumulated fractional movement. Outside the volumes, screen-space distance finds the nearest boundary; perspective interpolation preserves the corresponding world position. Geometry calculations use the game's current render matrix, including aspect-ratio changes and zoom.

**Focus visuals** shows the V1 line or the V2 hitboxes. The separate **Health bars → Hitboxes** drawing toggle remains independent. V2 reads the active hitboxes even when both drawing options are off. With V1 or Off selected, those additional reads stop unless Hitboxes drawing needs them.

The existing focus area, selected bone used to center target areas, target types, enemies-only filter, visibility, speed, scoped Vindicta speed, foreground and held-button checks still apply. Selection stays on the acquired target while eligible. Hitbox movement does not require physical mouse telemetry or changes to the Makcu protocol.

With prediction enabled, the whole hitbox set is translated by the existing projectile-intercept lead, and the nearest point is recomputed on that predicted set. Both the corresponding current point and predicted point must pass visibility. Free movement then applies to the predicted volumes. Disabling prediction applies it to the current volumes.

Players use their model's active animated hitboxes and disabled hit-group mask. Humanoid minions use animated hitboxes when available; minions without those bones use their transformed collision box. Soul orbs retain center-point focus. Missing or unstable player hitbox data pauses V2 for that target rather than silently changing to V1.

Existing settings keep V1. The new `free_focus_mode` setting stores `0` for V1 and `1` for V2; `free_focus` retains the enabled/disabled state. Only one mode can be active. `camera.json` and `makcu.json` report `free_movement_mode`; camera diagnostics include `hitbox_index` and `inside_hitbox`. The read-only `--probe` also writes `hitbox-focus-probe.json` with nearest-point geometry, projected hitbox-center intersections and timing, without sending mouse movements.
