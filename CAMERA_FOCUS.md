# Camera focus

**Adaptive** is the fourth movement choice, with independent regular/ultimate settings, angular or Target Zone acquisition, permitted hitbox regions, screen feedback and automatic prediction. It uses the current render projection without calibration. It remains opt-in; existing movement selections are preserved. See [Adaptive setup and behavior](docs/ADAPTIVE_FOCUS.md).

All camera controls are on the first sidebar tab, Camera Focus, marked by the crosshair-simple icon. Hold either mouse side button to select the closest enabled target in the center acquisition area. The target stays selected while eligible within retention. Losing it clears movement and starts a 100 ms delay before reacquisition while the button remains held. Camera Focus -> Target types enables players, lane/neutral minions and soul orbs independently. Its Visible only and Enemies only switches affect that focus profile. Health-tab visuals always exclude teammates, and health bars show both visibility states by default. Vindicta Ultimate retains its separate Visible only setting and always targets enemy players. Players are enabled by default. Regular focus's Enemies only filters players/minions in practice; replay allows both teams, and orb selection permits both securing and denying orbs.

Relative movement prefers the existing MAKCU serial backend. When MAKCU is unavailable, movement automatically falls back to the Windows `SendInput` API; background device discovery continues and MAKCU takes over after a successful handshake. The Camera Focus badge and Connection status identify the active backend. Menu opening, release, Alt-Tab, disabled settings, unsupported mode, lost/offscreen targets and stale samples older than 50 ms stop steering on both backends. Visible only additionally checks the actual focus point and the predicted point; blocked points pause movement while retaining the target. Full entity handles are revalidated before publishing. Dormant/dead NPCs and dormant orbs are excluded. Optional firing is controlled separately by Auto click.

Windows fallback supports relative movement, Auto click's mouse pulses and Auto Parry's configured keyboard key. It rechecks the active game window, minimization, Escape, activation and action deadlines immediately before submission. A separate Windows worker releases accepted buttons/keys after 35 ms, even while rendering or serial discovery is waiting. Menu opening, focus loss, backend changes and shutdown release owned Windows inputs; failed releases pause new submissions and are retried. Backend changes discard queued commands and reset focus response/selection state. `makcu.json` separates hardware connectivity from input readiness and records Windows movement, click, parry, release and failure counts. Windows may reject `SendInput` across different application privilege levels; rejected events are reported by Connection.

## Auto click

**Auto Click** has its own sidebar tab under Automation. Choose the profile, enable Auto click, then select **Enemy players** or **Soul orbs**. Enemy-player clicks require either side button. Soul-orb clicks default to **Automatic**; **Hold side button** is an optional activation choice. The click interval is 80–1,000 ms (default 150 ms). Only current, claimable orbs qualify, for securing or denying. Camera focus may be disabled, and its target-type checkboxes do not control Auto Click. Regular view and Vindicta Ultimate save independent toggles, target choices, orb activation and intervals; the validated Assassinate switch selects the active profile.

The ray comes from the current final render matrix and the game client area's center, including the existing zoom/aspect correction. The nearest eligible collision must qualify for the chosen target type, and the segment to that exact hit point must clear the loaded static map mesh. There is no nearest-edge or enlarged-FOV fallback. Team information must be valid, the local player must be alive, samples must be at most 25 ms old, and replays do not fire. Menu opening, Alt-Tab, manual left mouse, unavailable data and an active camera correction cancel pending clicks. Releasing the side button additionally cancels modes configured to require it. Auto click always requires visibility regardless of the camera's Visible only setting.

MAKCU V4's `km.click(1,1,35)` schedules one complete press/release pulse. Without MAKCU, Windows sends the down event and an independent worker sends its matching up event. Both routes enforce activation, freshness and interval at submission, and a generated pulse cannot activate left-mouse camera focus. Enabling Auto click adds its required hitbox/minion reads; selecting Soul orbs only also requests orb state and current collision geometry. The orb's `m_flAttackableTime`, `m_flEndAttackableTime` and verified client hit-prediction latch reject protected, expired, already-hit and unknown states. The attackable deadline also bounds queued input; prediction and Target Zone padding never authorize a shot. Diagnostics record the current reason and transmitted click count in `auto-click.json`; `--probe` writes player and orb ray diagnostics without opening MAKCU or sending input.

This is a current-position crosshair check. Primary projectile travel, weapon spread, third-person muzzle obstruction and dynamic cover are not simulated; the static mesh has the same limitations described in VISIBILITY.md. It does not establish that every resulting shot will hit.

## Vindicta Ultimate profile

X1 keeps the original saved focus speed. X2 has a separate speed slider and
starts with roughly twice the response speed: double the legacy speed, or half
the Adaptive response time. Adaptive X1 remains 1–100; X2 extends to 140. Legacy
X1 remains 1–120; X2 extends to 240. Existing movement limits still apply.
The Hold button selector offers X1 / X2, X1 only, X2 only and Left mouse.
X2 takes priority while both side buttons are held. Left mouse uses X1's speed.
Regular and ultimate profiles save these settings independently, and queued
corrections require the exact button that selected their speed to remain held.

**Movement → Hold button → X1 / X2 lock (360 deg)** is optional and saved
independently for regular focus and Vindicta Ultimate. X1 keeps its ordinary
area/controller. A fresh X2 press selects the nearest eligible angular target
across the full sphere, including behind the camera. It uses the existing X2
speed slider and the profile's movement/free region, prediction and filters.
The normal acquisition/retention area does not limit X2. Finite angular steering
turns toward offscreen/rear geometry through the same MAKCU/Windows transport.

The full target handle stays latched throughout that hold. Another target never
steals it. Death, disappearance, invalid geometry, failed visibility/prediction,
stale data, focus loss, menu opening or failed input end the lock. Releasing X2
and pressing it again is required for another selection; reappearing entities
do not resume the old hold. Scope/profile changes keep that same target while
eligible and cannot hand the held button to another controller. An empty initial
selection also requires a fresh press. Auto Click still uses only actual current
collision geometry, and its existing movement suppression includes X2 turns.
Diagnostics: camera-lock.json records target identity, release-required status,
requested counts, prediction, movement count and input failures.

The **Vindicta Ultimate** sidebar tab keeps independent focus speed, acquisition FOV/area, bone, hold button, Visible only, free movement and focus drawing settings. Its targets are fixed to enemy players, including in replay and when the separate profile is disabled while scoped. Copying regular controls, resetting or loading an old config cannot enable allies, minions or soul orbs for Assassinate. Its Auto Click profile is also restricted to enemy players. **Profile options** enables or disables the separate profile, copies regular controls, or resets only the ultimate controls. The profile activates automatically only while validated Vindicta Assassinate scope is active; selecting the tab does not activate it. Unscoping or changing hero restores regular settings and drops the previous selection. Hitbox reads and focus drawings follow the active profile.

The ultimate's area controls acquisition, not the game camera's field of view. Crosshair angle and Target Zone settings and their drawing toggles are remembered separately from regular focus. Health-bar/skeleton display controls remain independent. Assassinate is hitscan, so projectile travel prediction is off during its validated scope state; automatic compensation for timestamped sample age remains on in all movement modes. There is no Prediction panel in Vindicta Ultimate. Normal prediction settings resume after unscoping. See [hero/ability controls](docs/HERO_ABILITIES.md) for details.

Every movement mode offers Crosshair angle and Target Zone. Crosshair angle
measures degrees through the current camera; Target Zone expands current
permitted hitboxes by a percentage of full body height. Retention uses 1.3 times
the angular radius or zone padding. Leaving retention releases selection and
waits 100 ms before acquiring another target while the button stays held.
Previous Center FOV configs migrate to 6 degrees, and previous 2D/3D target
areas migrate to Target Zone with 12% padding; saved Adaptive settings stay intact.

## Free movement

Movement now offers Fixed bone, V1: Bone line and Adaptive. V2 configurations
migrate to Adaptive with Whole body. Adaptive's free-region choices are Head
and torso or Whole body; the former Selected bone region migrates to Head and
torso. Both profiles retain their separate bone, area and speed settings.

Camera Focus -> Free movement is optional and saved. The controller projects the head-body-pelvis polyline and finds the screen center's closest point. Movement along the line is free; an error perpendicular to the line or beyond its endpoints produces a correction to that nearest point. Perspective-correct interpolation recovers the world point for visibility and prediction. All three bones and positive projection depths are required. Turning it off returns to Head/Body/Pelvis focus.

This path reads no raw physical-axis stream: MAKCU's own corrections cannot be mistaken for manual vertical movement. With prediction enabled the same closest-point rule applies to the translated predicted line. Soul orbs always use their center. Minions use named bones when available; rigs without human bone names use their scaled/rotated collision center, which is a point rather than an invented bone line.

Focus visuals controls the selected point's gold ring and automatic bone dots/lines; turning it off leaves steering enabled. Skeleton switches remain separate. Focus speed is 1-120 (default 12); game sensitivity affects the response. Corrections use a bounded response and persistent fractional counts. Steering is checked before the GPU queue wait, but rendering and control still share the main thread.

## Skeletons and visibility

The Health bars header's Hold to display switch gates all visuals configured
in that tab. Click Key: H to choose another keyboard key or mouse button; the
initial key is H and the optional hold requirement is saved. This covers bars,
health numbers, skeletons, hitboxes, hero tags and the debug box. Camera focus
guides and other HUDs keep their own settings.

Health bars -> Skeletons uses the current model's parent indices and anatomical joint names, including spine, arms and legs. Helpers, fingers, cloth and weapon bones are excluded. Each segment checks both endpoints and its midpoint against the existing static map mesh. Clear segments are blue and blocked segments purple by default; both colors are editable in Skeletons & Hitboxes. Gray means visibility data is unavailable.

Skeletons display both visibility states by default, independently of Visible only.
Health bars → Skeletons & Hitboxes → **Hide visible skeletons** hides a player's
whole skeleton when its head, body or pelvis passes the existing visibility
check. Hidden players retain their skeletons; hitboxes and focus are unaffected.
The option is saved and starts off for existing installations. Static geometry
cannot account for every dynamic object or transparency effect; see VISIBILITY.md.
Skeleton transform sampling is enabled only while the feature is enabled.
NPC/orb discovery is enabled only for selected target types and updates every
100 ms; the default player-only scan remains once per second.

## Verified target data

This client's soul-orb RTTI class is CItemXP, registered as item_xp/xp_orb (also xp_orb_trooper). It is separate from dropped currency pickups. The current metadata gives CItemXP size 0xC50, launch time +0xC10, attackable time +0xC14, end attackable time +0xC18 and launch number +0xC1C. Selection uses scene state and presence, not these timing fields.

C_BaseEntity::m_pCollision is +0x340; +0x338 is the render component in this client. CCollisionProperty local mins/maxs are +0x40/+0x4C. CGameSceneNode::m_nodeToWorld at +0x10 contains position, scale and quaternion. These were checked against IDA metadata and live neutral minions/orbs. Model bone names/count/parents are +0x168/+0x178/+0x180; transform pointers and live counts retain the previously validated layout.

Live probes recovered 23 anatomical segments for each of four remote player models, lane and neutral minion targets, and a visible soul orb. These checks establish data extraction and visibility classification. The new free-line and expanded-target steering still require the user's in-game feel test; the previous fixed-bone MAKCU movement was user-confirmed.

## Automatic primary-weapon speed update

Camera Focus → Auto weapon speed now defaults to ON and persists. The read-only reader follows the local controller's current serial-validated pawn, its primary-weapon slot (21), current ability VData and the `primary` entry in its weapon-info tree. It reads the actual base bullet speed and movement-inheritance coefficient rather than a per-hero constant. Hero switches re-resolve this chain on subsequent samples; unavailable data never retains the previous hero's speed.

Bullet-velocity modifier 170 is read from the live aggregate mirror. Version, completed write sequence, result type, dirty state, context key and policy are checked; tick-sensitive policies also require the current tick. The mirror is populated even when the engine disables its cache-read optimization, so that optimization flag is not treated as mirror validity. The aggregate is read twice to reject concurrent changes. Absent modifier groups mean zero bonus; invalid/stale data means unavailable rather than zero. Controller, pawn, primary slot and VData references are rechecked before publishing.

The effective primary speed is base × (1 + bonus/100). The weapon coefficient scales local movement directly; the manual inheritance checkbox is ignored in automatic mode. GUI shows live speed, base, bonus and movement inheritance. Switching Auto weapon speed OFF restores the existing configured speed and manual inheritance option.

Ordinary primary bullets with supported modifier caching are covered. An active base-speed override (modifier 171), random-speed weapon, unsupported modifier policy, missing local pawn or invalid data pauses automatic prediction. Charged shots and ability-specific projectiles are not claimed to use the primary-gun profile. In replay without a local weapon, use manual mode. Basic camera focus remains available when automatic prediction data is unavailable.

Validation: 101 logic checks passed, including modifiers changing versions, incomplete writes, dirty/stale caches, tick freshness, context mismatch and invalid weapon values. Renderer/transparency and console cleanup tests passed, and the automatic controls preview was inspected. Live deployed diagnostics matched Vindicta with High-Velocity Rounds: base 25,984.3, bonus +60%, effective 41,574.9 units/s, inheritance 0. This update did not repeat the previously passed resource stress suite. All earlier statements that the running build only uses manual speed are superseded by this section.

Live hero-switch capture also observed distinct primary-weapon handles with base speeds 8,000, 32,600, 30,000, 62,500 and 25,000 units/s. Each published profile was valid and used its current speed; see auto-speed-live.json, auto-speed-item-live.json and auto-speed-current.json. The captures after switching heroes had zero bullet-speed bonus; the initial deployed Vindicta profile verified the +60% bonus branch. A same-hero buy/sell transition was not captured in these intervals.

Earlier experiments and build-specific mode addresses are preserved in docs/CAMERA_FOCUS_HISTORY.md. Their SendInput/raw-input descriptions are historical.

