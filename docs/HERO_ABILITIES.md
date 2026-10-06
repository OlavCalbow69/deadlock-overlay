# Hero detection and Vindicta's sniper

The Camera Focus tab shows the local hero automatically. **Hero / abilities** opens the detected hero's details and the **Vindicta sniper speed override** toggle and speed slider. The override uses its own camera-focus speed only while Vindicta is scoped with Assassinate; unscoping, changing hero or losing valid ability data restores the regular speed. Existing settings start with the same sniper speed as the user's regular speed.

Names come from the installed game's English hero localization and its live hero table. They are not inferred from a selected GUI option or the primary weapon. `Snapshot::hero` carries the current ID, name, internal token and sniper state for future hero features. Only Vindicta currently has an ability-specific focus profile.

Build 6753's `CCitadelHeroComponent::GetHeroData` at client RVA `0x766430` looks up spawned/loading/no-spawn IDs in the hero table: count `0x36A91F8`, data pointer `0x36A9200`. The local pawn's component is at `0x1620`; the spawned ID is component `+0x20`. The record's `CitadelHeroData_t::m_HeroID` must match. Record `+0x30`/`+0x38` contains sort/search localization tokens; Vindicta is ID 3, `hero_hornet`.

The reader resolves `CCitadel_Ability_Hornet_Snipe` from the local pawn's actual ability vector (`0x1440 + 0x68`), validates its full entity handle, and reads `m_flScopeStartTime` at `0x1FE4`. Scope code at `0xF80670` sets the timer; unscope code at `0xF9C8D0` clears it. A positive finite timer activates the override. Merely holding aim, owning the ultimate or having it selected does not activate it.

Hero metadata and ability identity are cached, with bounded caches. Switching pawn/hero or changing the ability vector invalidates the relevant cache. Failed or unstable reads publish no active override. Runtime diagnostics include the detected hero and scope state in `session.json`, and the effective speed in `camera.json`.

# Aspect overrides and projection

`r_aspectratio` can alter the field of view even when the window resolution stays the same. Build 6753's cached `ScreenTransform` matrix uses the legacy view's aspect override, while the final renderer uses the compact render view's aspect. With `r_aspectratio 2.3` on a 2560 x 1440 window, the cached matrix reported 2.3 but the actual render matrix reported 1.777778. Applying the cached matrix stretched overlay positions vertically.

The reader now uses the renderer's completed world-to-clip matrix directly: `CViewRender` at client `0x3644D60`, matrix at object `+0x298`. Constructor `0x092B90` identifies this object; `0x2165A60` builds the final matrix. Its vtable and binary hash are checked. The view's update flag and live FOV/aspect checks reject incomplete camera updates. There is no fixed game FOV, config-file rewrite or guessed FOV multiplier. Skeletons, markers, visibility camera origin and camera focus share this final projection, including normal aim and sniper zoom.

The Connection tab and Hero / abilities popup show the live render FOV and aspect. These are different from the camera-focus acquisition area's **Show FOV** control.

The skeleton's head-to-neck/upper-body connector is omitted; the head/head-end segment, shoulders, torso and limbs remain available.
