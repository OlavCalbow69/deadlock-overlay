# Build 6759 compatibility update

The 7 October 2026 installation changed client.dll, engine2.dll and worldrenderer.dll. The reader now accepts the verified build 6759 binaries and uses their current globals. Unknown hashes and ambiguous signature matches remain rejected.

The fresh schema contains 34,336 fields. All 20 required fields still match, including health, controllers, scene nodes, collision, the local hero/ability components and Vindicta's scope timer. Later pawn fields changed, but the reader does not use them. Code addresses and private layouts were checked separately against the actual DLL bytes and live read-only process data.

| Reader address | Build 6753 RVA | Build 6759 RVA |
| --- | --- | --- |
| Entity-system pointer | `0x3BF3BC0` | `0x3C4BA40` |
| Cached ScreenTransform matrix | `0x3C28C60` | `0x3C80AE0` |
| Final render view object | `0x3644D60` | `0x3689A70` |
| Render-view vtable | `0x2655AA0` | `0x26896B8` |
| Hero table count / pointer | `0x36A91F8` / `0x36A9200` | `0x36EDED8` / `0x36EDEE0` |
| Game-rules pointer | `0x3C23060` | `0x3C7AEE0` |
| Game-rules vtable | `0x268CD28` | `0x26BE898` |
| Primary weapon symbol | `0x34434A8` | `0x34883A8` |
| Modifier policy / fallback | `0x3B96240` / `0x3B9633C` | `0x3BEE0C0` / `0x3BEE1BC` |
| Global-variables pointer | `0x327D618` | `0x32C0670` |
| Hero-testing convar data pointer | `0x3689250` | `0x36CDDC0` |

The entity-system, ScreenTransform and global-variable signatures each match uniquely. Entity-system aliases agree in the running game. The final renderer retains matrix offset `+0x298`, compact camera header `+0x10` and update flag `+0x1330`; its static initializer is `0x092810`, and final matrix product is `0x2195400`. Projection continues to use the final rendered FOV/aspect for normal view and zoom.

Hero-data lookup is now at `0x767600`. Vindicta's scope/unscope functions moved to `0xFB5DB0` / `0xFD1FF0` and still write the timer at `+0x1FE4`. Their patterns were matched uniquely against the new DLL; the timer semantics and local ability-vector layout are unchanged.

This review also corrected stale bullet-speed reads. `EModifierValue` identifies bonus bullet speed as **171** and base speed override as **172**; 170 is ability projectile speed. Aggregate reader `0x129EF30` uses modifier-property version `+0x210`, dirty words `+0x214`, and 48-byte cache entries starting at `+0x400`. The reader now uses those verified values while retaining its sequence, version, context, dirty-state and double-read checks. An active base-speed override still reports unavailable rather than inventing a speed.

Engine connected-client and demo-player globals, and the renderer's active-world manager/vector/representation/world/resource layout, retain their addresses. Their vtables and running objects were checked before accepting the new hashes. Streamed arena resolution remains available when the engine reports bootstrap level `start`.

SHA256 profiles:

- client.dll: `b48636d0282a3f6916725e1701c0454738bb5a4903e83fc96a27b01dce800d23`
- engine2.dll: `084c45473667c65174a9a19c428359ac335c3e990008dbf26c0eef91be44784c`
- worldrenderer.dll: `bae38ed919214dcd74d49f9f01419f7ab2c86a57af666b4308f51241e9ff9888`

The updater's compatibility whitelist was rebuilt with the new client and engine hashes. The already refreshed map installation reports two updated meshes, eight current meshes and no export failures. Its fresh schema validates with no required-field differences.

## Verification

Release passed all five CTest suites: logic, renderer, console shutdown, resource checks and data updater. Logic checks passed 656 cases. Resource checks showed no private-memory, handle, GDI or USER object growth across three cycles of 2,200 frames.

The installed runtime connected to build 6759 in `new_player_basics`, with five controllers, four remote players, 80 skeleton segments, valid projection and no invalid handles or missing anchors. Its visibility mesh contains 203,973 triangles and distinguishes clear and blocked segments. Hero detection followed live hero changes; Bebop's primary weapon reported 20,000 units/s. Makcu reconnected on COM3 at 4,000,000 baud without write failures. The extended reader benchmark averaged 1.19 ms over 200 samples in the tested scene.

This update did not repeat a live-match streamed-map transition, demo playback, a bullet-velocity item buy/sell test or Vindicta scope-speed acceptance test. Their relevant binary layouts were reviewed; no mouse movement was sent automatically during verification.

The previous IDA database was preserved locally as a build 6753 archive. A database copied into a folder named `6759` can still contain old bytes: the input path or on-disk DLL hash alone does not prove which code the database contains.
