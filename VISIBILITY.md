# Static map visibility

Health bars → **Visible only** is enabled by default and persists with settings. It applies to drawing and camera focus. Existing preferences are preserved.

The sampler normalizes the live map name and loads `maps/<map>.tri` beside the EXE. The file contains little-endian, headerless triangles: three XYZ float32 vertices per triangle, 36 bytes each. Invalid, empty, oversized or missing files fail closed while the option is enabled. Unavailable camera position also fails closed. Disable Visible only to restore the previous behaviour.

Each mesh is immutable and shared by snapshots. A balanced BVH is built on the sampler thread once on map change, not on the render thread. Missing files retry after five seconds. Leaving a map releases its geometry after outstanding snapshots finish using it; only the current map is cached. During loading, stale player samples cannot steer the camera. The projection matrix is refreshed after loading.

Visibility rays run from the matrix-derived camera position to head/body/pelvis. At least one clear bone keeps a player's healthbar eligible; blocked dots are omitted and connecting segments require both endpoint bones to be clear. Focus acquisition requires its selected bone to be clear. While holding a target, the exact interpolated point and predicted focus point are checked separately. A blocked point pauses movement and clears fractional motion; the same selected target can resume when clear. This does not automatically switch targets behind walls.

The four installed meshes were extracted from this machine's map VPKs using ValveResourceFormat 20.0.6980 and the included map-export helper. Newer `m_compounds` children are decoded explicitly because the released package predates compound support. Hulls are triangulated by face loops; meshes preserve their triangles; capsules/spheres are tessellated. Part bind poses are applied. Trigger/playerclip/npcclip/sky and window-only geometry is excluded. Solid/default geometry and line-of-sight/foliage blockers are included. Source SHA256 and extraction counts are recorded in each `.tri.json`.

| Map | Triangles | One-time load/build | Average ray test |
| --- | ---: | ---: | ---: |
| hero_testing | 104,142 | 37 ms | 1.15 µs |
| dl_midtown | 2,806,627 | 1,316 ms | 2.39 µs |
| dl_hideout | 483,158 | 193 ms | 0.92 µs |
| new_player_basics | 205,905 | 78 ms | 1.34 µs |

These are synthetic benchmark rays, not measured in-game frame times. Each test used 2,000 rays after loading. 512 known-triangle crossing tests passed, including both ray directions. 380 general logic checks passed, covering thin/nearby walls, parallel rays, invalid input, finite segment limits, BVH branches and missing meshes. Rendering/transparency and console shutdown passed. The GUI resource stress test passed three 2,200-frame cycles (private bytes 95,510,528→95,539,200; handles 1,651→1,652; GDI 7→7; USER 13→13). That stress test exercises GUI resources, not long-session map switching.

This is static collision visibility, not exact rendered-pixel visibility. Dynamic doors, destructible objects, moving props and ability-created barriers are not updated. Collision shape/material approximations can differ from what is visually opaque. Re-extract after map updates; the current mesh files belong to the recorded installed VPK versions. The start/menu map has no usable physics aggregate and is intentionally absent. Unsupported/custom maps need their own extraction. Deadlock reopened after deployment. The reader loaded dl_hideout with 483,158 triangles and a valid camera origin; the user then confirmed that markers disappear behind a wall and holding a side button stops camera focus. Dynamic-obstacle behaviour remains outside this implementation.

Diagnostics: `visibility.json` records map/status/triangle count, ray time, camera position, each player's three visibility bits and whether the focus point is occluded. `--visibility-test` runs known-wall checks and the synthetic benchmark, then exits.

Parser: [ValveResourceFormat](https://github.com/ValveResourceFormat/ValveResourceFormat). Compound layout follows its [Compound.cs](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/Resource/ResourceTypes/RubikonPhysics/Shapes/Compound.cs).
