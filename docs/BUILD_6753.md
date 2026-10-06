# Build 6753 compatibility update

The 6 October 2026 installation changed client.dll, engine2.dll and worldrenderer.dll. The old reader correctly reported `Unsupported client.dll: update required` because its binary profile belonged to the previous patch. Refreshing maps or dumping schema cannot update compiled code addresses.

The fresh dump contains 33,929 fields. The used base-entity, controller, scene-node, collision, skeleton, ability-component, weapon-map and game-mode schema fields are unchanged. Client global addresses were resolved individually from executable signatures, string initialization or RIP-relative references.

| Reader address | Previous RVA | Build 6753 RVA |
| --- | --- | --- |
| Entity-system pointer | `0x3BE8840` | `0x3BF3BC0` |
| Projection matrix | `0x3C1D760` | `0x3C28C60` |
| Game-rules pointer | `0x3C17B60` | `0x3C23060` |
| Game-rules vtable | `0x26859B0` | `0x268CD28` |
| Primary weapon symbol | `0x3438B28` | `0x34434A8` |
| Modifier policy table | `0x3B8ACF0` | `0x3B96240` |
| Modifier fallback policy | `0x3B8ADEC` | `0x3B9633C` |
| Global-variables pointer | `0x3273918` | `0x327D618` |
| Hero-testing convar data pointer | `0x367E8B0` | `0x3689250` |

Live reads confirmed that the signature-derived entity pointer, generated SDK pointer and RTTI-discovered pointer identify the same entity system. The primary symbol is `primary`; game-rule RTTI, mode fields and the hero-testing convar agree with their expected types and layouts.

The engine's connected-client and demo-player addresses retain their layouts. The renderer's active-world vector, manager/world/representation vtables, world pointer and resource-name pointer retain their layouts. They were checked against the running game before accepting the new module hashes.

The SHA256 profiles are:

- client.dll: `678aec94adb44623e335ee7ec76c08f4477a0ea88a85cea5cb70abace1bacaa8`
- engine2.dll: `aac84e48de57844d5499af8fd95c976143efe2f14845ff2409b111eb9ff5ce74`
- worldrenderer.dll: `2e8dd9057d381c0d097e6fe12e822796cfff4672c1b1eda6b25cd277dd756d5e`

Unknown hashes, ambiguous signatures and addresses that disagree with the validated profile remain rejected. Map/schema update tools use the updated client and engine hashes for their compatibility report.

## Verification

Release was rebuilt and all five CTest suites passed: logic, renderer, console shutdown, resource checks and data-update checks. The rebuilt updater also accepted the actual fresh dump with 33,929 fields and no required-field differences.

The installed runtime connected to the current game in `dl_hideout`, read three controllers and two remote players with 44 skeleton segments, and reported no invalid handles or missing anchors. The Hideout mesh contained 483,158 triangles and camera projection was valid. Live primary weapon speed was 25,984.3 units/s with no velocity bonus, and Makcu connected on COM3 at 4,000,000 baud.

The active-world renderer path was checked directly through read-only process inspection. This update did not repeat a live-match map transition, replay playback or bullet-velocity item buy/sell test; no mouse movement was sent during verification.
