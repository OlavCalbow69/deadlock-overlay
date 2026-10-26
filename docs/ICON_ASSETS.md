# Game icon assets

The installed game's assets contain the icons; a DLL database is not needed to export them. Source archive:

`X:\SteamLibrary\steamapps\common\Deadlock\game\citadel\pak01_dir.vpk`

Keep the numbered `pak01_###.vpk` files beside that directory VPK. The directory file indexes content stored in those chunks.

Paths confirmed in this installation:

- `panorama/images/heroes/*_mm_psd.vtex_c`: hero minimap portraits.
- `panorama/images/heroes/*_sm_psd.vtex_c` and `*_sm_png.vtex_c`: small hero portraits.
- `panorama/images/heroes/hornet_mm_psd.vtex_c`: Vindicta's internal Hornet asset name.
- `panorama/images/npcs/trooper_psd.vtex_c`: trooper image.
- `panorama/images/npcs/`: NPC images, including subfolders such as `npcs/neutrals/`.
- `panorama/images/minimap/npc_marker_enemy_psd.vtex_c`, `npc_marker_friend_psd.vtex_c`, `npc_marker_neutral_psd.vtex_c`: category markers.
- `panorama/images/minimap/neutral_small_psd.vtex_c`, `neutral_medium_psd.vtex_c`, `neutral_large_psd.vtex_c` and `super_neutral_marker_psd.vtex_c`: neutral markers.

553 PNGs were exported successfully: 411 hero assets (including cards, vertical and hashed variants), 32 NPC assets, 75 minimap textures, 31 overview textures and four rasterized powerup SVGs. This includes 20 images under `npcs/neutrals/`. The previous exporter only selected flat `_mm`/`_sm` hero names and flat `npcs/` entries; it missed the subfolders and other image roles.

Files are under `data/icons/`. `manifest.json` records source paths and decoded dimensions. `catalog.json` maps 67 hero definitions and 80 NPC definitions to image references. `INDEX.md` is the readable hero index. These definition counts include disabled/development entries and are not the number of playable heroes.

`by-hero/<id>-<display name>/` provides `small.png`, `minimap.png`, `card.png`, `critical.png` and `gloat.png` where configured. Names come from the installed English localization, including its `:n` nominative suffix. Verified examples:

| Display name | Definition token | Folder |
| --- | --- | --- |
| Vindicta | `hero_hornet` | `by-hero/3-Vindicta/` |
| Rat King | `hero_ratking` | `by-hero/84-Rat King/` |
| Solomon | `hero_chessmaster` | `by-hero/85-Solomon/` |
| Violet | `hero_artist` | `by-hero/86-Violet/` |
| Baba | `hero_baba` | `by-hero/88-Baba/` |

All image references for non-disabled hero definitions resolved. Two unresolved references belong to disabled/development `hero_boho` (minimap and card); three disabled/development definitions configure no images. These remain explicitly unavailable rather than being given another hero's artwork.

`by-npc/<definition>/icon.png` provides configured NPC images. Normal, medic and melee lane troopers use the same exported trooper portrait as a **generic fallback**, explicitly labeled in the catalog. Other neutral units use their configured `m_strCustomUnitIcon` when present. There is no claim that every lane variant has a unique portrait. The catalog reports 27 other NPC definitions without a configured or selected generic icon; it does not silently substitute unrelated images.

Hero references are taken from `scripts/heroes.vdata_c` (`m_strIconImageSmall`, `m_strMinimapImage`, `m_strIconHeroCard`, critical/gloat fields). NPC references come from `scripts/npc_units.vdata_c` (`m_strCustomUnitIcon`). Build 6819's client schema and the finished IDA database confirm these fields. The client also contains a helper constructing `panorama/images/heroes/icons/%s_png.vtex`; a code reference alone does not establish that every such asset ships in the current VPK.

Repeat the export after a game update with the same ValveResourceFormat dependency as the map tool:

```powershell
dotnet run --project tools/icon-export -c Release -- "X:\SteamLibrary\steamapps\common\Deadlock\game\citadel\pak01_dir.vpk" "data/icons"
```

The exporter recursively covers `heroes/` (excluding weapon artwork under `guns/`), `npcs/`, `minimap/` and `overviews/`. Four installed powerup vectors are also rasterized. `--catalog-only` rebuilds the index and friendly folders from an existing successful manifest when only the localization/catalog rules changed; a normal full export is appropriate after a game update. `coverage.json` reports unresolved references and definitions without images. `runtime.tsv` maps hero IDs, engine subclass StringToken hashes and map names to local PNGs and configured breakable respawn intervals.

Alternatively, [Source 2 Viewer](https://github.com/ValveResourceFormat/ValveResourceFormat) opens the VPK and exports compiled textures to PNG. Its [CLI documentation](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/docs/guides/command-line.md) describes filtered archive extraction and texture export.

`GameIcons` now caches D3D11 shader-resource views on the overlay's device, including failed lookups, and preserves alpha/aspect ratio. The cache is bounded and reloads when the runtime catalog changes. NPC subclass hashes were compared with live lane, neutral and breakable entities. Missing individual portraits use a category marker; hero artwork is never borrowed from a different hero.

**Update all data** and **Install all maps** regenerate this catalog when the installed directory VPK changes and publish it to both the project and runtime directories. The executable build copies an existing `data/icons` directory. Game assets stay local and are excluded from Git.
