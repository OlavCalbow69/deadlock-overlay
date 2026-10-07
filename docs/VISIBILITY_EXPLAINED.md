# How this overlay's visibility check works

Updated 6 October 2026 for build 6753. This describes the implementation in this repository, including the distinction between “player partly visible” and “the exact focus point is clear.”

## 1. Export static collision geometry

The map-export helper reads the installed map VPK through ValveResourceFormat 20.0.6980. It looks for `world_physics.vphys_c`, or a model's PHYS aggregate where applicable. This supplies physics/collision geometry rather than screenshot pixels or the game's GPU depth buffer.

It converts hull faces to triangle fans, copies mesh triangles, and approximates spheres/capsules with tessellation. The latter use 32 segments and two nine-ring hemispheres; degenerate pole triangles are removed. Part bind poses transform shapes into map/world coordinates. Compound children are decoded explicitly because the packaged parser version predates that layout. Non-finite vertices and degenerate triangles are rejected.

Shape tags determine what enters the mesh. Empty tags and tags including `solid`, `CONTENTS_SOLID`, `world`, `default`, `opaque`, `blocklos`, or `Citadel_Foliage` qualify. A shape tagged only as a window, player clip, trigger, sky or other nonqualifying category is omitted. Mixed tags still qualify when any included tag is present. Thus “window-only excluded” is more precise than saying all glass is always excluded.

Output is `maps/<map>.tri`, headerless little-endian float32 data. Each triangle stores nine floats (three XYZ vertices), 36 bytes. The adjacent `.tri.json` records source VPK hash, selected physics resource, shape statistics and bounds. It is useful provenance; the runtime does not continuously compare that source hash with the installed VPK. Re-export after map updates.

| Included map | Triangles |
| --- | ---: |
| hero_testing | 104,142 |
| dl_midtown | 2,806,627 |
| dl_hideout | 483,158 |
| new_player_basics | 205,905 |

The menu/start map lacks a usable physics aggregate and is absent. Unsupported maps need their own export.

## 2. Select and load the current map

The external reader obtains the level name from the engine connection state. Directly loaded maps use that name. In a live streamed match, however, the engine can report `start`: that is the bootstrap level, which has no collision mesh. In this case the reader resolves the gameplay arena from the active world renderer's `CSingleWorldRep` list. In the validated live match, the list contained `maps/start/world`, `maps/dl_midtown/world`, and several `maps/scenes/.../world` entries; only `dl_midtown` selects the arena mesh. This uses the actual loaded resource name, rather than a hardcoded `start` → `dl_midtown` alias.

The streamed reader accepts the canonical `maps/<map>/world` resource shape and ignores bootstrap, UI and portrait-scene paths. Multiple distinct arenas, invalid list headers or pointers, and a list changing during the read produce a waiting/error state rather than selecting an arbitrary map. It validates worldrenderer.dll SHA256 `bae38ed919214dcd74d49f9f01419f7ab2c86a57af666b4308f51241e9ff9888` and the manager/world-representation/world-instance vtables before using this private layout. A different renderer binary can require a profile update even when the schema fields still match.

The resolved map key normalizes slashes, strips directories and the extension, lowercases the basename and accepts only a bounded alphanumeric/underscore/hyphen name. For example `maps/dl_midtown.vpk` becomes `dl_midtown`. This produces a mesh filename beside the executable rather than using an arbitrary raw path from process memory. Full world scans are cached for up to one second, with list and selected-world identity checks between scans. Changes invalidate that cache. Names are read in small batches with a byte fallback at page boundaries.

On a map change, the reader drops its current mesh reference and loads the new one. The file must be nonempty, divisible by 36, and contain no more than eight million triangles. Coordinates must be finite and within the configured magnitude limit of ten million units. Degenerate triangles are filtered; an empty result is invalid.

Loading and BVH construction happen on the sampler thread once per map, not inside every draw call. They can temporarily delay fresh sampling—Midtown historically took about 1.3 seconds—while the UI remains responsive. Stale snapshots are gated from steering, and the projection matrix is refreshed after loading before publishing the new sample.

Missing or invalid meshes retry after five seconds. With “Visible only” enabled, missing geometry or an invalid camera produces no permission to draw/focus through it. Disabling that setting bypasses the visibility restriction. The sampler currently still computes available visibility even when that setting is off.

Meshes are immutable and held with shared ownership by snapshots. Switching maps releases the old mesh when the last outstanding snapshot releases it. There is no permanent cache of every visited map. A valid loaded mesh is refreshed after an in-menu data update or map transition; an unrelated manual file replacement requires restarting the overlay or triggering an update.

## 3. Build a tree so each ray is cheap

Testing all 2.8 million Midtown triangles for every bone would be wasteful. The loader builds a balanced bounding-volume hierarchy (BVH).

Each node stores an axis-aligned bounding box enclosing its triangles. A leaf contains at most eight triangles. Larger groups split at the median triangle centroid along the largest extent of their bounding box, then recurse into two children. The centroid comparison uses the sum of a triangle's vertices, which gives the same ordering as dividing by three.

At runtime a segment first tests a node's box. If it misses, all triangles below that node are skipped. If it hits, traversal descends; only intersected leaves need triangle tests. The search exits on the first obstruction because it needs a clear/blocked answer, not the nearest hit's material or distance.

This is CPU work. It does not stall the GPU to fetch rendered depth. BVH building consumes time and memory once; repeated visibility queries reuse it.

## 4. Recover the camera position and read bone positions

Bone transforms supply world coordinates for head, body and pelvis. The reader validates the model, bone indices, transform pointers and sampled entity before using them. Projection is a separate step that converts those coordinates to screen positions.

The visibility ray starts at the rendered camera position recovered from the world-to-projection matrix, rather than simply assuming the pawn's origin is the eye. If `a`, `b`, `c` are the XYZ parts of rows 0, 1 and 3, and their fourth components are `aw`, `bw`, `cw`, the camera center `C` satisfies:

```
a · C = -aw
b · C = -bw
c · C = -cw
```

The implementation solves this three-by-three system with cross products/determinant checks. A determinant too close to zero or non-finite result invalidates the camera origin. This assumes the sampled matrix has the expected perspective-camera structure.

For a third-person camera, the recovered camera center differs from the pawn and weapon muzzle. Therefore this check answers whether the camera has a line to a point. It does not prove a projectile fired from the muzzle can reach that point.

## 5. Test a finite segment against map triangles

For source `S` and target `T`, set `D = T - S`. Points on the segment are:

```
P(t) = S + t * D, with 0 <= t <= 1
```

A wall behind the target must not block it, so an infinite ray would be wrong. The code uses a finite interval and ignores only 0.001 world units at each endpoint to avoid exact-surface contact artifacts. This is a very small world-space epsilon, not a large distance where nearby walls are skipped. A valid segment shorter than 0.002 units is considered clear.

Box tests use the slab method: intersect the valid `t` interval with the box's X, Y and Z intervals. An almost-parallel axis is handled by checking whether the source is inside that slab.

At leaves, a double-sided Möller–Trumbore test checks each triangle. It forms the two triangle edges, rejects a nearly parallel determinant, calculates barycentric coordinates, rejects coordinates outside the triangle, and accepts a hit only inside the finite segment interval. “Double-sided” means walls can obstruct from either direction. Some scalar calculations are promoted to double, but vector dot/cross operations still use floats; this is not a full double-precision geometry engine.

If any triangle intersects, the segment is blocked. If traversal finishes without a hit, it is clear. Invalid geometry/camera/coordinates do not produce a clear result while filtering is enabled.

## 6. Apply the answer to drawing

Each player gets three independent visibility bits: head, body, pelvis. Overall player visibility is their logical OR.

- With “Visible only” on, a fully blocked player is omitted.
- A player with any clear tested bone can keep its health bar, even if the head itself is behind a wall.
- Each dot is drawn only when that dot's bone is clear.
- A connecting line segment requires both endpoint bones to be clear.

This is a partial-player eligibility rule, not per-pixel clipping. The bar can be positioned above the head/head-end while only the pelvis is clear. Checking both line endpoints also does not prove every point between them is unobstructed. The renderer does not stencil each overlay pixel against the game's depth buffer.

Independent drawing settings still apply. Turning off focus visuals hides the automatic focus dots/guide without disabling focus movement; separately enabled bone markers can remain visible.

## 7. Apply the answer to camera focus

Acquiring a player requires the selected anchor bone to be clear. When free vertical movement is enabled, the held focus point interpolates between head, body and pelvis and stays within that range.

The controller then checks the exact interpolated point against geometry. If prediction is enabled, it also checks the predicted destination point. Consequently, clear original bone endpoints are not enough to authorize steering toward an interpolated or predicted position behind a wall.

A blocked focus point pauses mouse movement, clears fractional movement remainders and marks the current point invalid. The selected player can remain held and resume when the point is clear; occlusion does not automatically choose another player through the wall. Losing the entity or its valid screen/target state has separate release/reacquisition behavior. GUI input capture, mode permissions, freshness and held-button state can also stop movement independently of visibility.

The three sampled bone rays are measured in the reader's visibility timing. Additional exact/predicted-point checks performed by the focus controller are outside that particular timing measurement.

## Evidence and limits

The known-map test exercised 512 crossing checks (128 per installed mesh, including both directions), plus 2,000 benchmark rays per mesh. Historical average ray times ranged about 0.92–2.39 microseconds. A historical live capture across 117 samples reported about 0.075 ms average, 0.137 ms maximum for the sampled player-bone visibility phase. Those are measured scenarios, not a universal frame-cost guarantee.

The relocated build passed its logic, rendering/transparency, console-shutdown and resource tests and reran the map visibility test. The user also confirmed an actual wall hides markers and stops held-button focus. Synthetic wall tests prove intersection behavior and selected map data, not exhaustive material or map coverage. GUI resource tests do not by themselves prove every long-session map-switch leak is absent.

The main limitations are static collision geometry and approximate opacity. Moving doors/props, destructible objects and ability-created walls are not updated from live state. Collision hulls can differ from visual meshes; foliage and curved-shape approximations can block or clear differently from rendered pixels. Window-only geometry can be omitted. Network/bone sampling and frame timing can cause transient differences. Camera visibility is not muzzle visibility, and the exported mesh is not an authoritative server line-of-sight result.

Source locations in the copied project:

- `tools/map-export/Program.cs`: export, tags, compounds and triangulation.
- `src/overlay/visibility.h`: file format validation, map normalization, BVH and segment intersections.
- `src/overlay/game_reader.cpp`: map selection, camera origin, bone visibility and snapshot publication.
- `src/overlay/focus_math.h`: camera/projection mathematics.
- `src/overlay/main.cpp` and `replay_camera.h`: draw filtering, exact focus point, prediction and input gates.

Diagnostics beside the executable include `visibility.json`, with map/status, triangle count, ray time, camera origin, each player's bits and focus occlusion. `--visibility-test` runs map crossing checks/benchmarks and exits.
