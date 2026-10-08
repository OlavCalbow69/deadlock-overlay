# Updating maps and schema

Open the overlay menu with **Insert**, then choose **Connection**.

- **Update all data** installs new or changed map meshes, creates a fresh schema dump, resolves addresses and installs a validated reader profile. Load sandbox, bots or replay with at least one alive remote player before updating; the final read-only check needs real player/model data. Accept the Windows administrator prompt for the update helper.
- **Install all maps** handles map files only. It can find Deadlock through the running process or Steam library configuration, and does not need administrator approval. New map VPKs are discovered on each click.
- **Dump schema** creates a fresh dump and reader profile without exporting maps. It also needs a loaded game and Windows administrator approval.
- **Cancel** stops the update. **Report** opens the job's directory with its progress log, `status.ini`, and (for schema updates) `dumper.log`.

The menu and rendering continue while data tools run in a separate process at below-normal priority. Closing the overlay cancels its helper and active dumper. If an update is interrupted, the next launch shows that state and offers another attempt.

The helper scans the installed `game/citadel/maps` directory, including nested map directories, and ignores numbered VPK archive fragments. It uses the bundled exporter to read `world_physics.vphys_c` or `world_physics.vmdl_c`. Meshes are validated against the source VPK SHA256, exporter version, triangle count, and mesh SHA256. Unchanged meshes are reused; changed, new or invalid meshes are extracted in staging before installation. VPKs without usable collision physics, such as `start.vpk`, are reported as skipped. Duplicate map names and extraction failures are reported explicitly.

In this source checkout, meshes are kept in `project/maps` and installed beside `build/Release/DeadlockOverlay.exe` in `maps`. Successful updates refresh the reader's map cache automatically. There is no need to recompile the overlay for a new map. Static map limitations still apply to moving/destructible geometry.

If a previously exported map now fails extraction or loses its physics data, its old mesh is retained but marked unavailable. Visible-only drawing fails closed for that map until a later successful install clears the marker.

Schema output is kept in `project/data/schema-dump/deadlock`; the previous successful output is retained as `deadlock.previous`. The helper rejects missing/incomplete SDK output and records the exact client/engine hashes. Failed or cancelled dumps do not replace the last successful dump. A successful dump is retained even if subsequent profile validation fails, so it is available for review or another validation attempt.

## Automatic reader profiles

The reader loads field offsets and globals from `data/reader-profiles/<client-sha256>.ini` beside the EXE. Profiles are also saved in the project's data directory. All required values must be present, and the client, engine and world-renderer hashes and resolver version must match. Profile parsing and DLL hashing happen on attach/reload, rather than per frame. The previously verified 6759 build remains available as a compiled fallback when no external profile exists.

The helper obtains schema fields by class/field name, locates globals with unique executable signatures and RIP-relative references, checks agreement between alternate signatures, and resolves primary vtables through RTTI. Private model, hitbox, projection, weapon-map and modifier-cache layouts are checked against code witnesses that preserve their member offsets and strides while allowing relocated references. Replay and streamed-world checks also validate selected virtual methods. A DLL hash change alone no longer requires changing the EXE.

Before publication, the helper invokes `--validate-profile` in a separate read-only process. It checks the actual projection, entity handles, head/body/pelvis anchors, skeletons, hitboxes and available hero/weapon metadata. This path creates no overlay and sends no mouse input. A missing signature, conflicting address, changed private-layout witness, malformed profile or failed live check leaves the last working profile installed. The report names the unresolved part; genuine layout/code changes can still require a reader/resolver update.

Successful publication is atomic per profile file, retains a `.previous` backup, and automatically reconnects the running reader. No EXE recompilation is needed for patches the resolver can validate. The button does not download project code or update the Steam game itself.

## Command line

From the executable directory:

```powershell
.\DeadlockOverlay.exe --update-data maps
.\DeadlockOverlay.exe --update-data schema
.\DeadlockOverlay.exe --update-data all
```

From `build/Release`, retry profile resolution using the last successful dump, without exporting maps or dumping again:

```powershell
.\tools\data-update\DeadlockDataUpdate.exe --mode profile --root . --project ..\.. --job .\data\updates\profile-retry
```

The saved dump must belong to the current DLL files. This command is useful after a main-menu dump when you have now loaded a scene with other players.

The schema/all commands request elevation for the worker, leaving the normal overlay unelevated.

## Rebuilding the bundled tools

The packaged updater is self-contained and needs no .NET installation on the user's PC. Rebuilding it requires a .NET 10 SDK; rebuilding the bundled dumper requires the same Visual Studio C++ toolchain as the overlay.

```powershell
.\build.ps1 -RebuildDataTools -DotnetPath "C:\path\to\dotnet.exe"
```

Alternatively, set `DEADLOCK_DOTNET` or install .NET 10 on PATH. A normal `build.ps1` run reuses already packaged tools. After changing updater/exporter source, use `-RebuildDataTools` to refresh that package.

The pinned dumper source and its upstream reference are documented in `tools/schema-dumper/UPSTREAM.md`. If a future patch changes the dumper's schema discovery itself, that tool must be updated before a new dump can succeed.
