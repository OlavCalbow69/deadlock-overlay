# Build 6793 reader update

The schema dump for client SHA256 `d61348c564ce931e969996af18dbdec8544643810f28f3d330fb94b33231c460` succeeded. Reader generation stopped at `game_rules` because all three signatures extended beyond the end of their functions into unrelated neighboring functions. Those neighbors changed in 6793. The signatures now end at the original function's return instruction, retaining the complete relevant code and unique RIP-relative references. All three agree on the game-rules global in both the archived 6774 DLL and the current 6793 DLL.

Four melee virtual-method witnesses also shifted by three slots. Their resolver now permits a reviewed search of at most four slots in either direction, requires exactly one matching slot, and validates the duration method's virtual call against the independently resolved attack-data slot. This preserves code and layout checks while handling small insertions into the class's virtual table. Other witnesses still use their exact slots. Changed logic or ambiguous matches continue to stop profile publication.

The generated 6793 profile includes the new schema component offsets and the current client, engine and world-renderer hashes. The overlay and updater must be deployed together because the resolver manifest changed.

Map export also failed for `dl_hideout`, `dl_midtown` and `new_player_basics`: ValveResourceFormat 20.0 could not decode KV3 type 26. Both exporter projects now pin 20.1.7467, whose [upstream release adds KV3 types 26 and 27](https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.1). Exported meshes still require matching source/mesh hashes before publication; a failed export cannot enable visibility with stale geometry.

Violet is resolved from the live hero table as ID 86, token `hero_artist`. Her current model is `models/heroes_wip/artist/artist.vmdl`, with 233 bones and a 20-hitbox default set. Every hitbox bone binds to that model's skeleton. Its ordinary head/spine/pelvis names use the existing rig resolver; no hero-name whitelist is needed.

Validation: direct CMake Release build and .NET updater publish succeeded. Read-only profile validation in the running game passed with Violet selected, five controllers, four remote players, no missing anchors, valid hero/weapon data, and a primary base speed of 19,500 units/s. The existing self-test, CTest and resource/stress suites were not run.

The installed updater then exported all three changed maps successfully: `dl_hideout` (483,193 triangles), `dl_midtown` (2,852,765), and `new_player_basics` (203,973). Seven existing meshes remained current; 144 scene/portrait archives without usable world physics were skipped. No map exports failed. The restarted overlay loaded the generated 6793 profile and reports `Static map ready`. MAKCU was disconnected during this check, so no input automation was exercised.
