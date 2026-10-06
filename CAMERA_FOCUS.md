# Camera focus

All camera controls are on the first sidebar tab, Camera Focus, marked by the crosshair-simple icon. Hold either mouse side button to select the closest enabled target in the center acquisition area. The target stays selected for that hold; release and press again after it disappears or to select another. Camera Focus -> Target types enables players, lane/neutral minions and soul orbs independently and includes the shared enemies-only and visible-only filters. Players are enabled by default. Enemies only filters players/minions in practice; replay allows both teams, and orb selection permits both securing and denying orbs.

Relative movement uses the existing MAKCU serial backend. Menu opening, release, Alt-Tab, disabled settings, unsupported mode, lost/offscreen targets, stale samples older than 50 ms and unavailable device input stop steering. Visible only additionally checks the actual focus point and the predicted point; blocked points pause movement while retaining the target. Full entity handles are revalidated before publishing. Dormant/dead NPCs and dormant orbs are excluded. No firing is automated.

## Free movement

Camera Focus -> Free movement is optional and saved. The controller projects the head-body-pelvis polyline and finds the screen center's closest point. Movement along the line is free; an error perpendicular to the line or beyond its endpoints produces a correction to that nearest point. Perspective-correct interpolation recovers the world point for visibility and prediction. All three bones and positive projection depths are required. Turning it off returns to Head/Body/Pelvis focus.

This path reads no raw physical-axis stream: MAKCU's own corrections cannot be mistaken for manual vertical movement. With prediction enabled the same closest-point rule applies to the translated predicted line. Soul orbs always use their center. Minions use named bones when available; rigs without human bone names use their scaled/rotated collision center, which is a point rather than an invented bone line.

Focus visuals controls the selected point's gold ring and automatic bone dots/lines; turning it off leaves steering enabled. Independent marker and skeleton switches remain separate. Focus speed is 1-120 (default 12); game sensitivity affects the response. Corrections use a bounded response and persistent fractional counts. Steering is checked before the GPU queue wait, but rendering and control still share the main thread.

## Skeletons and visibility

Health bars -> Skeletons uses the current model's parent indices and anatomical joint names, including spine, arms and legs. Helpers, fingers, cloth and weapon bones are excluded. Each segment checks both endpoints and its midpoint against the existing static map mesh. Clear segments are blue and blocked segments purple by default; both colors are editable in the preview card. Gray means visibility data is unavailable.

Skeletons deliberately display both visibility states even with Visible only enabled. That filter continues to gate health bars, markers and focus. Static geometry cannot account for every dynamic object or transparency effect; see VISIBILITY.md. Skeleton transform sampling is enabled only while the feature is enabled. NPC/orb discovery is enabled only for selected target types and updates every 100 ms; the default player-only scan remains once per second.

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

