# Vanta controls after visual cleanup

Visuals retains Vanta's original player drawing paths. Its horizontal sections are **Health bars**, **Skeletons / Hitboxes**, **Hero tags**, **World** and **Minimap**. The bindable display key applies to player visuals. World markers and the minimap have independent enablement. Bar orientation was not changed; the horizontal layout refers to section navigation.

- Health bars retain the one-pixel green fill, black outline, world-space spacing, health numbers, hide-when-visible option and debug placement box.
- Skeletons/hitboxes retain Vanta's geometry, visibility colors and hide-visible skeleton option.
- Hero tags retain their below-feet placement, frame styling and hide-when-visible option. They can use hero portraits, and distance can be enabled with or without a tag.

Imported boxes, labels, effects, radar, styled bars and world markers were removed. World markers and the minimap have now been rebuilt with fresh positions, shared projection and game assets; see the world/minimap notes. Scripts, spin, template provisioning and Active Reload remain removed. Old saved keys for removed features are ignored and omitted on the next save. Existing Vanta preferences retain their values.

## Retained optional features

Camera Focus and Vindicta Ultimate retain expanded targeting controls, independent profiles, X1/X2 speeds and X2 single-target lock. Tracking provides custom activation, cycling/locking, named joints and closest/random pools, response filters, framing offsets and guide styling. V1 retains its original head-to-pelvis line. Adaptive remains free within permitted geometry, with bone preference used only for near ties. Focus Area retains Crosshair angle, Target Zone, pixel circle and pixel square.

Vindicta Auto Select replaces the old low-health/scoped-shot helpers. It monitors visible enemy heroes below 49% health, ranks the minimum estimated lethal charge and acquires only targets estimated killable by 60% charge-ring fill. It recalculates throughout the engagement; actual current geometry under the game crosshair authorizes a shot. Automatic scopes close after a confirmed shot or target loss, while manually opened scopes remain open. Missing defenses are identified as estimate omissions, and missing essential data pauses the controller. See [Auto Select](VINDICTA_AUTO_SELECT.md). Assassinate has zero projectile-flight lead. Auto Click retains soul-orb claimability protection. Auto Parry retains melee/Counterspell handling and optional Puddle Punch detection.

HUDs contains the event/status panels and optional crosshair. Vindicta Ultimate separates Camera, ordinary Auto Click and independent Auto Select into three sub-tabs; the general Auto Click page edits regular-view settings only. Appearance retains the render cap, which limits painting while input handling keeps its existing pacing. Configurations retains named save/load/delete, remembered selection and auto-save. Loading named presets disables automation, including Auto Select, while restoring preferences. Default-settings startup keeps its previous policy.

## Input and data

MAKCU and Windows fallback share one input owner. Hero-helper key pulses use that worker; parry preempts them. Menu opening, focus loss, minimization, stale data, disconnect, preset loading and controller changes cancel queued actions and release owned keys. Owned input cannot activate another helper. X2 needs a confirmed physical release before another lock acquisition.

Schema fields and private layouts remain validated by the updater. Optional missing capabilities pause their feature without disabling the core reader. Observer mode uses one byte. Puddle effective properties use verified constructor registrations in the current DLL.

`--diagnostics` writes ordinary session/feature diagnostics. `--external-probe <file.json>` observes reader capabilities without opening an input worker or overlay. Build Release directly through CMake. Do not run legacy self-test, CTest or resource/stress suites unless explicitly requested.

See [icon assets](ICON_ASSETS.md) and [world/minimap behavior](WORLD_OVERLAY_RECODE.md).
