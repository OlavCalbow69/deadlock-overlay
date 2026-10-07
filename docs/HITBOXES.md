# Animated player hitboxes

Open Health bars and enable **Hitboxes**. The switch is independent of Skeletons and health bars, persists in settings, and respects Enemies only. The Visible and Blocked swatches color both skeletons and hitboxes. Both visibility states are drawn; Visible only continues to filter bars, dots and camera focus. A hitbox's center determines its static-map visibility color. Gray means that visibility is unknown.

The reader uses the model's actual hitbox definitions. It draws oriented boxes, spheres and capsules, including head hitboxes that are intentionally omitted from the custom skeleton. Bone position, quaternion and uniform scale transform the shapes each sample. Translation-only definitions follow their bone origin without rotation or scale. This is client animation data; it does not display the server's historical, lag-compensated hitboxes.

## Build 6759 layout

The current client bytes and read-only live process data were checked together. The original ComputeHitboxBounds pattern has one match at client RVA `0x1611840`. Its helper at `0x1622630` maps the definition's bone-name hash to the model's bones and copies a 32-byte CTransform. The external reader binds the corresponding model bone by its name.

| Data | Layout |
| --- | --- |
| Scene model binding / bone transforms | `scene + 0x1E0` / `scene + 0x1C0` |
| Model meshes / mesh masks | count `+0x70`, pointers `+0x78`, masks `+0x90` |
| Scene body-group mask / active hitbox set | `+0x348` / `+0x40C` |
| Mesh hitbox-set records | pointer `+0x168`, count `+0x174`, allocation `+0x164`; stride `0x48` |
| CHitBoxSet within each record | record `+0x18`; name hash `+0x20`, count `+0x28`, boxes `+0x30`, capacity `+0x38` |
| CHitBox runtime stride | `0x70` |
| CHitBox bone name / bounds | `+0x10`, min `+0x18`, max `+0x24` |
| Radius / group / shape / translation-only | `+0x30` / `+0x38` / `+0x3C` / `+0x3D` |
| Disabled hit groups | pawn `+0xB98`, 32-bit mask |

Shape values are Box=0, Sphere=1 and Capsule=2. Sphere bounds supply its center; capsule bounds supply its two endpoint centers. Capsule endpoints do not need to be ordered like box minima/maxima.

CSkeletonInstance obtains the selected set name from mesh zero through `0x21CE7A0` / `0x21BE8D0`, then uses that name's hash with the body-group mask. The reader preserves that lookup order instead of drawing duplicate sets from every render mesh. `CModel::GetHitboxSets` at `0x21BE930` and `0x21BEFF0` establish the mesh filtering and set layout.

Definitions are cached only when Hitboxes is requested, with bounded model, mesh, set and shape counts. Failed metadata reads retry at most once per second. Dynamic transforms are read with the existing bone-position batch. Invalid descriptors, transforms, disabled groups, and changes to the model, set or body mask during a sample are rejected. Disabling the toggle removes hitbox reads and drawing work. Whole-shape frustum rejection and fixed wire meshes avoid offscreen tessellation and per-shape drawing allocations; individual edges are clipped before perspective division.

The public [CHitBox schema](https://s2v.app/SchemaExplorer/deadlock/modellib/CHitBox) and [ValveResourceFormat hitbox parser](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/Resource/ResourceTypes/ModelData/Hitbox.cs) supplied format references. Those published schema revisions are older than this installation, so runtime offsets and strides were verified against build 6759 rather than accepted from the website alone.

## Verification

Release compiled successfully. The drawing check exercised rotated boxes, spheres and capsules, and the new menu layout was visually inspected. The live build read 72 capsule hitboxes across four nearby bots, with all sets ready and no missing anchors. With hitboxes, skeletons, minions and soul orbs enabled together, 200 reader samples averaged 1.41 ms in the tested scene. Logical and updater self-tests were skipped; normal build scripts now require `-SelfTests` to run them.

After installation, runtime diagnostics confirmed 18 hitboxes submitted to the overlay for an onscreen bot. Hitboxes-only sampling took 0.43 ms in that snapshot, and rendering reported 222 FPS against the 240 Hz display target. These are observations from the current sandbox scene, not a guarantee across maps or GPU workloads.
