# Hero name tags

Open **Health bars → Hero name tags** and enable **Hero name tags**. **Hide when visible** defaults on: tags appear only on hidden enemies. Turn it off to tag visible enemies too. The header's **Hold to display** switch controls tags together with bars/numbers, skeletons, hitboxes and the debug box. The initial binding is **H**; click the key button to rebind it. With the switch off, eligible enabled features draw continuously. Camera focus visuals retain their separate controls.

The tag displays the hero's localized name below the player origin at their feet. It uses the overlay's Geist Semibold font, the menu's theme colors, rounded corners and a subtle border. Appearance's **Transparent frame** and **Frame opacity** settings also control tag backgrounds; there is no separate opacity value to keep in sync.

This is independent of health bars, skeletons and hitbox drawing. Teammates, dead/dormant players and unknown teams are excluded. Tags require valid hero/foot data. When Hide when visible is on, loaded map visibility and a valid camera are also required; a clear head, torso, pelvis or sampled active hitbox suppresses the tag. Unknown visibility never counts as hidden. When the switch is off, visibility does not filter tags. Tags follow the normal overlay focus and fresh-snapshot gates, and do not receive mouse input or consume H.

Enabling tags requests current hitboxes even when their outlines are disabled. Hero labels are cached per pawn and model, refreshed every 500 ms, and dropped when entities disappear or the feature is disabled. Remote hero identity reads do not modify the local Vindicta ability cache. Existing reader-profile fields supply all data; no new DLL address or schema contract is required.

The game's training-dummy hero (ID 55) has empty sort/search name fields. It uses the shipped `hero_targetdummy` localization entry instead of producing an empty tag.

Settings: `hidden_enemy_tags` and `hide_visible_enemy_tags`; shared activation uses `health_visuals_hold` and `health_visuals_key` (Windows virtual key, initially H). The old `hidden_enemy_tags_hold_h` initializes shared activation only when the new setting is absent and is no longer saved. Invalid/reserved keys fall back to H. Health bars independently save `hide_visible_healthbars`, initially off. Diagnostics: `enemy-tags.json` records activation, bound key, hide-when-visible, drawn count and shared frame settings; `session.json` includes remote hero names and foot anchors.
