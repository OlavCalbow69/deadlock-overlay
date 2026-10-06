# Map export helper

Build with a .NET 10 SDK:

```
dotnet build MapExport.csproj -c Release
dotnet bin/Release/net10.0/MapExport.dll "path/to/map.vpk" "path/to/overlay/maps"
```

This reads local VPK files, writes headerless float32 `.tri` files and source metadata, and does not modify the game. It uses ValveResourceFormat 20.0.6980 (MIT) through NuGet, with explicit compound decoding based on the current upstream shape layout. The exported data is game asset data, not covered by the parser's code license. Export for personal local use; source archives omit game meshes.

The shared extraction code is in `MapExporter.cs`. The overlay's packaged data updater uses it directly, discovers installed maps, validates source/mesh fingerprints and installs new or changed meshes from staging. Use Connection → Install all maps for routine updates; see `docs/GAME_DATA_UPDATES.md`.

The overlay's static-map limitations and validation are documented in VISIBILITY.md. Output is written to a temporary file before replacement; failed extraction must not be used as a map mesh.
