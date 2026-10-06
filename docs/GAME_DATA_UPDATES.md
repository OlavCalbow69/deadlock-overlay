# Updating maps and schema

Open the overlay menu with **Insert**, then choose **Connection**.

- **Update all data** installs new or changed map meshes and runs a fresh schema/SDK/signature dump from the running game. Start Deadlock and wait for its main menu first. Accept the Windows administrator prompt for the update helper.
- **Install all maps** handles map files only. It can find Deadlock through the running process or Steam library configuration, and does not need administrator approval. New map VPKs are discovered on each click.
- **Dump schema** runs a fresh dump without exporting maps. It needs Deadlock running and Windows administrator approval.
- **Cancel** stops the update. **Report** opens the job's directory with its progress log, `status.ini`, and (for schema updates) `dumper.log`.

The menu and rendering continue while data tools run in a separate process at below-normal priority. Closing the overlay cancels its helper and active dumper. If an update is interrupted, the next launch shows that state and offers another attempt.

The helper scans the installed `game/citadel/maps` directory, including nested map directories, and ignores numbered VPK archive fragments. It uses the bundled exporter to read `world_physics.vphys_c` or `world_physics.vmdl_c`. Meshes are validated against the source VPK SHA256, exporter version, triangle count, and mesh SHA256. Unchanged meshes are reused; changed, new or invalid meshes are extracted in staging before installation. VPKs without usable collision physics, such as `start.vpk`, are reported as skipped. Duplicate map names and extraction failures are reported explicitly.

In this source checkout, meshes are kept in `project/maps` and installed beside `build/Release/DeadlockOverlay.exe` in `maps`. Successful updates refresh the reader's map cache automatically. There is no need to recompile the overlay for a new map. Static map limitations still apply to moving/destructible geometry.

If a previously exported map now fails extraction or loses its physics data, its old mesh is retained but marked unavailable. Visible-only drawing fails closed for that map until a later successful install clears the marker.

Schema output is kept in `project/data/schema-dump/deadlock`; the previous successful output is retained as `deadlock.previous`. A fresh dump is validated before replacing the current one. The helper rejects missing/incomplete SDK output, compares the reader's required schema fields, and records client/engine binary hashes in `overlay-validation.json`. Failed or cancelled dumps do not replace the last successful dump.

**A fresh dump does not automatically certify support for a new game patch.** Global addresses, code signatures and private layouts are separate from schema fields. Unknown client/engine hashes produce a clear “reader profile review” result, while retaining the fresh dump for that review. The existing reader's binary whitelist remains enforced. The buttons update game data; they do not download or execute new project code from GitHub.

## Command line

From the executable directory:

```powershell
.\DeadlockOverlay.exe --update-data maps
.\DeadlockOverlay.exe --update-data schema
.\DeadlockOverlay.exe --update-data all
```

The schema/all commands request elevation for the worker, leaving the normal overlay unelevated.

## Rebuilding the bundled tools

The packaged updater is self-contained and needs no .NET installation on the user's PC. Rebuilding it requires a .NET 10 SDK; rebuilding the bundled dumper requires the same Visual Studio C++ toolchain as the overlay.

```powershell
.\build.ps1 -RebuildDataTools -DotnetPath "C:\path\to\dotnet.exe"
```

Alternatively, set `DEADLOCK_DOTNET` or install .NET 10 on PATH. A normal `build.ps1` run reuses already packaged tools. After changing updater/exporter source, use `-RebuildDataTools` to refresh that package.

The pinned dumper source and its upstream reference are documented in `tools/schema-dumper/UPSTREAM.md`. If a future patch changes the dumper's schema discovery itself, that tool must be updated before a new dump can succeed.
