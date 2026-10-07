# Hero detection and Vindicta's sniper

The Camera Focus tab shows the local hero automatically. **Hero / abilities** opens the detected hero's details and the **Vindicta sniper speed override** toggle and speed slider. The override uses its own camera-focus speed only while Vindicta is scoped with Assassinate; unscoping, changing hero or losing valid ability data restores the regular speed. Existing settings start with the same sniper speed as the user's regular speed.

Names come from the installed game's English hero localization and its live hero table. They are not inferred from a selected GUI option or the primary weapon. `Snapshot::hero` carries the current ID, name, internal token and sniper state for future hero features. Only Vindicta currently has an ability-specific focus profile.

Build 6759's `CCitadelHeroComponent::GetHeroData` at client RVA `0x767600` looks up spawned/loading/no-spawn IDs in the hero table: count `0x36EDED8`, data pointer `0x36EDEE0`. The local pawn's component is at `0x1620`; the spawned ID is component `+0x20`. The record's `CitadelHeroData_t::m_HeroID` must match. Record `+0x30`/`+0x38` contains sort/search localization tokens; Vindicta is ID 3, `hero_hornet`.

The reader resolves `CCitadel_Ability_Hornet_Snipe` from the local pawn's actual ability vector (`0x1440 + 0x68`), validates its full entity handle, and reads `m_flScopeStartTime` at `0x1FE4`. Scope code at `0xFB5DB0` sets the timer; unscope code at `0xFD1FF0` clears it. A positive finite timer activates the override. Merely holding aim, owning the ultimate or having it selected does not activate it.

Hero metadata and ability identity are cached, with bounded caches. Switching pawn/hero or changing the ability vector invalidates the relevant cache. Failed or unstable reads publish no active override. Runtime diagnostics include the detected hero and scope state in `session.json`, and the effective speed in `camera.json`.

# Aspect overrides and projection

`r_aspectratio` can alter the field of view even when the window resolution stays the same. Build 6753's cached `ScreenTransform` matrix uses the legacy view's aspect override, while the final renderer uses the compact render view's aspect. With `r_aspectratio 2.3` on a 2560 x 1440 window, the cached matrix reported 2.3 but the actual render matrix reported 1.777778. Applying the cached matrix stretched overlay positions vertically.

The reader uses the renderer's completed world-to-clip matrix directly: build 6759's `CViewRender` at client `0x3689A70`, matrix at object `+0x298`. Static initializer `0x092810` identifies this object; `0x2195400` builds the final matrix. Its vtable and binary hash are checked. The view's update flag and live FOV/aspect checks reject incomplete camera updates. There is no fixed game FOV, config-file rewrite or guessed FOV multiplier. Skeletons, markers, visibility camera origin and camera focus share this final projection, including normal aim and sniper zoom.

The Connection tab and Hero / abilities popup show the live render FOV and aspect. These are different from the camera-focus acquisition area's **Show FOV** control.

Skeletons omit all head/head-end and neck segments. Both shoulders connect to the highest joint of the remaining drawn spine, even when their original parent was the neck or a lower spine joint. The chest-to-pelvis spine, arms and legs remain. The menu preview uses the same connections as the live skeleton.
