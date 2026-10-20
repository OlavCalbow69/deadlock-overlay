# Settings layout

The existing menu shell, fonts, icons, colors, transparency and glass cursor
are retained. Settings are grouped in the sidebar:

- Game: Camera Focus, Vindicta Ultimate, Health bars.
- Automation: Auto Click, Auto Parry.
- System: Connection, Diagnostics (previously Test).

Camera Focus contains Movement, Targets, Focus Area and Prediction panels.
Vindicta Ultimate has Movement, Targets and Focus Area; there is no Prediction
panel. Assassinate has zero projectile travel lead and automatic sample-age
compensation. Advanced response remains inside Movement. Each tab edits its
own settings; ultimate override, copy and reset are under Profile options.

Every movement mode offers Crosshair angle and Target Zone. Radius uses degrees;
zone padding uses percent of active body height. Both controller families use
the same current-space zone primitives for activation and preview. Previous
Center FOV settings migrate to a 6-degree angle; previous 2D/3D target areas
migrate to Target Zone with 12% padding. Existing Adaptive choices remain intact.

Movement choices are Fixed bone, V1: Bone line and Adaptive. The former V2
setting migrates to Adaptive with Whole body; saved enum value 2 for Adaptive
stays unchanged. Adaptive offers Head and torso or Whole body. Former Selected
bone region settings migrate to Head and torso in both camera profiles.

Movement shows separate X1 and X2 speed sliders side by side. X1 retains the
profile's original speed; X2 starts faster and has a higher maximum. Hold button
selects both side buttons, X1 only, X2 only, Left mouse, or X1 / X2 lock (360 deg).
Left mouse shares X1's speed. Lock mode keeps X1 normal and gives X2 one eligible
target per press with no angular/zone boundary. Its existing X2 slider is reused.
Loss requires releasing and pressing X2 again. Movement/free regions and the
profile's visibility/team/target filters still apply. The same controls apply
independently to Vindicta Ultimate. The new option is off for older configs.

Auto Click has a dedicated page. Profile chooses which settings to edit;
the Active profile badge identifies the one currently selected by the game.
Activation contains the enable switch, target choice, activation mode and
interval. Shot rules explains the checks, and Live status shows the current
gate and input backend. Automatic activation is available for soul orbs;
enemy-player clicks require a side button. The orb activation preference is
saved as auto_click_orbs_hold_side, with the independent vindicta_ult_ prefix.

Health bars contains three cards: Health bars, Skeletons & Hitboxes, and Hero
name tags. The first has bar enablement, optional health numbers, Hide when
visible, a world-space Box gap slider and Debug placement box. The geometry card contains its drawing
switches and Visible/Blocked colors. Hero name tags are directly visible.
The header's Hold to display switch applies to all Health-tab features,
including numbers and the debug placement box. Key: H is the initial binding;
click it, release held buttons, then press a keyboard key or mouse button to
rebind. Escape cancels; Insert remains the menu key. With the hold switch off,
enabled features draw normally. The key and hold preference are persisted;
the old tag-only hold preference initializes this shared switch once.
Glass cursor and frame transparency remain in the header's
Appearance menu. There is no live preview or standalone bone-marker control.

All Health-tab player visuals always exclude teammates. Health bars draw both
visibility states by default; Hide when visible makes them hidden-only. Hero
tags have a separate Hide when visible switch, enabled by default to preserve
their earlier behavior. Turning it off allows tags on visible enemies too.
This tab has no Visible only or Enemies only switch. Regular Camera Focus and Vindicta Ultimate keep their own Visible only
switches, and regular focus retains its configurable team filter. Assassinate
is always restricted to enemy players; its target-type and team controls are
fixed, including after copying or resetting the profile.

The sole health-bar style is one physical pixel of solid green surrounded by
a one-pixel opaque black border. It is vertical, left of the player, and spans
the lowest to highest animated body joints. Green fills upward and stays green
at low health. Cached anatomical joints share the existing contiguous bone
read, independently of skeleton drawing; no cloth or weapon helpers enter the
bounds. The actual render matrix accounts for distance, zoom and custom aspect
ratio. The projected top, bottom, leftmost and rightmost joints define a 2D
body box. Box gap starts at 0.25 m and projects a camera-facing horizontal
offset at the leftmost joint's depth; its visual size changes with zoom and
proximity. healthbar_box_gap_meters stores the distance. Pixel-gap selection
and the legacy horizontal renderer/settings have been removed.

Debug placement box draws the exact pixel-enclosing body bounds used for the
bar, even with health bars disabled. It shares the bounds read and projection,
adds no raycasts, and is saved independently as healthbar_debug_box.

Skeletons & Hitboxes also contains Hide visible skeletons. This independent,
saved option hides an entire visible player's skeleton while preserving hidden
players' skeletons and leaving hitboxes unchanged.

Auto Parry operates automatically while enabled and the game is focused. The
side-button hold setting and control are removed; old parry_hold_side keys are
ignored. Auto Click and Camera Focus retain their own activation controls.

The --menu-preview <directory> command renders layout PNGs into a directory
using hidden surfaces. It does not read the game, start an input worker, open
MAKCU or save settings.
