# Vindicta Auto Select

Open **Vindicta Ultimate → Auto Select**, the third sub-tab after Camera and Auto Click. The feature starts disabled and has independent angle, guide and speed settings. Existing camera and ordinary click settings are preserved. Named presets disable Auto Select when loaded.

While Vindicta is selected, it monitors enemy heroes strictly below 49% health. Acquisition requires a visible head or torso point inside its Crosshair angle and estimated lethal damage by 60% charge-ring fill. That ring fill represents 80% shot power. An existing engagement can charge longer if defenses or health change; it releases an enemy that becomes impossible even at full charge.

The controller ranks minimum required charge, angular correction and health, randomizing exact ties. It recalculates while scoping and charging. Only a fresh intersection of the actual crosshair with the selected enemy's current permitted hitbox can authorize a shot. Assassinate uses no projectile-flight prediction. Automatic scopes close after shot confirmation or target loss; manually opened scopes stay open.

Damage comes from current named ability definitions, upgrades and live Spirit modifiers, with independently checked effective caches where available. The optional reader capability also supplies defenses and known protection states. Missing conditional modifiers or defenses are listed under **Calculation details**; those calculations remain estimates. Unmodeled damage procs are excluded. Missing essential ability data pauses the feature.

Movement, scope taps and shots use the existing input owner and MAKCU/Windows fallback. Auto Parry retains priority. Overlay menus, the visible game cursor, Escape, focus loss, minimization, disconnects and stale data cancel inputs. A brief incoherent render sample releases queued input while retaining scope ownership metadata for at most 500 ms. Predicted zero scope times require a stable 100 ms closure before ownership is discarded.

## Validation on 2026-10-24

- Release build and 43 focused calculation, geometry, settings and scope-continuity checks passed. The existing self-test, CTest and resource/stress suites were not run.
- Current DLL reader profile regenerated and read-only validation passed.
- Unscoped modifier path returned valid damage data in all 49 recorded samples, including an item bonus for charged abilities.
- Sandbox Windows fallback check: selected a 352-HP dummy, scoped, aimed, confirmed one shot, exited the automatic scope and released program-owned keys. Evidence: `verification/assassinate-auto-select/scope-continuity-check.json`.
- The three sub-tabs were rendered into hidden menu captures for layout inspection. Those captures send no game inputs.
- MAKCU was unavailable during the successful live check; its hardware firing path and the full remaining sandbox scenario matrix have not been verified in this session.
