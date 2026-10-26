# World markers and minimap

These are new Vanta render paths. The removed imported radar/world cache is not used.

## Controls

Visuals → Hero tags offers hero icons instead of names and independent distance display. These retain below-feet placement, frame transparency/opacity, the hide-when-visible preference and the bindable player display key.

Visuals → World enables icon markers for lane minions (including an allied option), jungle NPCs, soul/buff breakables, available ground pickups and buff spawners. Range, icon size, distance and waiting/respawn display are optional.

Lane-minion outlines use their own faction rather than local allegiance: Archmother / Sapphire is light blue; Hidden King / Amber is orange. The same colors apply to world markers and minimap rings. Unknown teams and destroyed objects remain neutral gray; teammate filtering is unchanged.

Visuals → Minimap is independently enabled. It can show players, teammates, world categories and colored lanes. All detected enemy players appear regardless of visibility, as requested. Turning off 3D world markers or player visuals does not disable the map. Category choices are shared with World. Size, corner, opacity and hero-icon size are configurable.

The main map uses the installed HUD's midtown map texture; other maps use their own exported overview when available. Placement uses replicated GameRules minimap bounds (+X right, +Y up). Missing bounds pause the map. Missing art leaves a grid with markers; another map's texture is never substituted. Colored lanes use installed map paths from `data/map-lanes`, with replicated zipline paths as a fallback. The map cache is shared with Rift, reloads after updates and clears on map changes. York is yellow (1), Broadway blue (4), and Greenwich green (3 or legacy 6). The map still authors Greenwich as lane 6, and old localization keys call it purple; its current display color and name are green / Greenwich. Paths are clipped by the minimap viewport so segments crossing its border remain visible. This does not read team fog-of-war or expose non-replicated entities.

## State and timers

- Moving NPC positions, player roots and projection come from each fresh reader snapshot. Only identities, model information and image metadata are cached at slower rates.
- Health/life state distinguishes intact and destroyed boxes. One failed read, dormancy or disappearance never starts a timer.
- An estimated box timer begins only after the same static object was seen alive and then dead in two samples. The interval is read from its matching installed VData definition; practice overrides are used when configured. `~` explicitly identifies this estimate. There is no private-layout assumption about a generic VData pointer.
- No estimate is invented for an object already destroyed on joining, unknown definitions or missing clock data. Reappearing intact geometry resets the estimate; map/process changes and clock rewinds clear history.
- Buff spawners use the replicated active flag and next-drop timestamp. Neutral camps use the replicated origin/next-spawn arrays and the same match-clock calculation as the existing Mid Boss HUD. These timers have no `~` prefix.
- Ground pickups must be active, not finished and not vacuum-assigned. Floating `CItemXP` soul orbs remain separate and retain the existing auto-click claimability protections.

World fields are optional schema capabilities. Missing fields do not invalidate an otherwise supported core profile. Build the matching updater and regenerate the current reader profile when the feature contract changes.

## Assets and performance

`data/icons/runtime.tsv` maps hero IDs and engine StringToken subclass hashes to installed assets. NPCs without a configured portrait use a generic category icon; no unrelated hero portrait is substituted. `GameIcons` bounds the positive/negative SRV cache, decodes each used image only once and releases textures with its renderer. Asset catalogs reload after an update. Update all data and Install all maps refresh them from the local VPK.

New displays are disabled by default, and saved Vanta settings are preserved. Disabled world/minimap displays request no world-object reads. The map never supplies targeting or click geometry.

## Focused verification

Release was built directly with CMake. Focused checks cover independent configuration persistence, map transforms, invalid geometry, path constraints, Source meters, observed destruction, disappearance, respawn identity and history reset. The current schema/profile passed read-only core validation. A read-only live check matched lane, jungle and box subclass IDs to installed definitions, and captured current pickup/spawner state and map bounds.

`--world-preview <directory>` captures the live read-only map/marker renderer into a hidden surface; it starts no input worker and sends no game inputs. It also captures a main-map fixture with lanes on and off using this build's validated bounds. The current live Midtown snapshot provides three installed paths: yellow York left, blue Broadway center and green Greenwich right. Captures verify drawing over the installed Midtown texture; its catalog retains the legacy `caldera` key. Focused map-cache checks cover map changes, updater replacement, missing files and invalid geometry. A live destruction/respawn transition still needs ordinary in-game observation. `--menu-preview <directory>` captures the real menu. No legacy self-test, CTest or resource/stress suite was run.
