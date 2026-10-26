# Hero preview and Solace controls

The visual preview is an owned tool window beside the menu. It follows the menu when dragged, remains available across pages, and hides when the menu closes or loses game/menu focus. It creates no taskbar entry. On smaller screens, the menu and preview scale together to fit the monitor's work area. **Appearance → Visual preview window** toggles it; it starts enabled.

The sample is the installed Vindicta model, resolved through `scripts/heroes.vdata_c` and exported from `pak01_dir.vpk`. This installation resolves to `models/heroes_staging/hornet_v3/hornet.vmdl_c`. The local package contains textured meshes, the model's rig and its default hitbox set. It uses a static authored pose, not live game animation or a substituted mannequin. The current export has 138,103 triangles, 63 bones and 16 hitboxes.

Health bars, numbers, skeletons, hitboxes, placement bounds, hero tags/icons and distance use Vanta's existing drawing functions and the same projection as the model. Preview controls change only the sample's health, occlusion and viewing angle. They do not enable visual features or affect the game. Display-hold bindings do not hide this menu sample. The sample does not preview world objects, the minimap or automatic input.

The textured model is rendered to a cached D3D11 image. It redraws only when its viewing angle, viewport size or asset changes; the preview window is painted at up to 60 Hz. `--diagnostics` includes `visual-preview.json`, and ordinary frame timing includes preview submission/presentation.

## Local assets

`data/preview/vindicta.vpreview` is a bounded local mesh/rig package. **Update all data** and **Install all maps** refresh it when the installed VPK or exporter version changes, alongside the icon catalog. `build.ps1` copies existing preview data into the runtime folder. A missing model reports its status in the preview instead of replacing the sample with unrelated artwork. Game assets remain ignored by Git.

The reusable exporter is `tools/preview-export/GamePreviewExport.cs`; its package writer is `PreviewPackage.cs`. These are linked into the packaged updater. The separate exporter project also supports manual model export for development. The preview itself needs no game addresses.

Exporter version 2 reverses both of VRF's Source-to-glTF axis rotations. Mesh positions/normals and the Source rig now share the same basis. The orbit offset preserves the saved Turn slider's viewpoint. The mesh, skeleton and hitboxes rotate together.

The preview and menu each own their glass-cursor trail, shader and backdrop. The preview draws its cursor on its foreground layer, above the model, overlays and controls. Switching between the two windows does not reuse a texture from the other D3D device.

## Compact HUDs and rifts

Mid Boss, Unstable Rift, Parry, Session and Spectators use the same compact rounded card, colors and frame opacity as the menu. The left stack places Rift below Mid Boss and Parry below Rift; Session and Spectators stack on the right. The dedicated **HUDs** menu page contains these toggles, custom-crosshair settings and **Preview HUDs**. Rift has a **!** indicator with an **In testing** hover tooltip. The side preview's **HUDs** page shows sample cards independently of feature enablement, with an optional Live data switch.

Rift data is sampled at 10 Hz only when enabled. The reader uses the replicated GameRules cash-in/give-up state and the local team's `CLASS_KOTH_SPAWNER` marker. `Update all data` resolves these optional fields, marker stride and named warning cvars; missing optional data does not disable the core reader. Lane comes from the marker when available, otherwise the nearest installed map lane path. The updater exports those paths from map VPK entities into `data/map-lanes`, and the reader reloads them automatically.

The client does not replicate the next randomized spawn deadline. An observed clear-to-warning transition starts an **estimated** countdown (prefixed `~`) using the live early-warning and opening-warning durations. Joining midway shows Incoming rather than resetting a countdown. Active fade timing uses the replicated GameTime deadline; gaps, clock rewinds and map changes invalidate estimates. Lane remains unannounced when no trustworthy nearby path is available. The sandbox dev command uses a custom early-warning duration, so its estimate can differ from a normal match; the active deadline remains authoritative.

For an event check in Explore NYC, `citadel_koth_dev_test_spawn` starts the warning and teleports the player to a map rift location. The standalone hero range has no rift location. Move away from the circle to avoid immediately capturing it.

## Shared controls

The clean reference is [Free Solace ImGui Interface](https://github.com/poncippg-spec/Free-Solace-ImGui-Interface), commit `6b88e2ed65b06ffbfa26b1aa4561579707fbeb4f`. Vanta reuses its animated selects, switches, sliders, checkboxes, input field and pill buttons. Compact buttons retain the same drawing/animation code with an explicit height. Native color editors and list/header primitives use the matching Solace palette.

The shared select widget now owns queued option labels and applies selections while the caller's value is alive. Previously, temporary option arrays and enum adapters could expire before the deferred overlay draw, causing the minimap dropdown crash. A selected row also consumes its click so it cannot activate a control underneath.

Animation state is scoped to each ImGui context so the main menu and preview can update independently. Hidden, targeted menu/model capture paths exercise dropdown selection and preview controls without a game reader, input worker, desktop input or settings writes. Legacy self-test, CTest and resource/stress suites are not needed for this change.
