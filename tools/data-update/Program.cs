using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

if (args.Contains("--self-test")) return UpdateTests.Run();
var options = Options.Parse(args);
if (options is null) { Console.Error.WriteLine("Usage: DeadlockDataUpdate --mode maps|schema|all|profile --root <exe-directory> --job <job-directory> [--project <project-directory>] [--parent <pid>] [--maps <map-directory>]"); return 2; }
using var cancellation = new CancellationTokenSource();
using var monitorStop = new CancellationTokenSource();
var updater = new DataUpdater(options, cancellation.Token);
var monitor = Task.Run(async () => {
    Process? parent = null;
    try {
        if (options.Parent != 0) parent = Process.GetProcessById(options.Parent);
        while (!monitorStop.IsCancellationRequested) {
            if (File.Exists(Path.Combine(options.Job, "cancel")) || (parent is not null && parent.HasExited)) { cancellation.Cancel(); return; }
            await Task.Delay(200, monitorStop.Token);
        }
    } catch (OperationCanceledException) { }
    catch { cancellation.Cancel(); }
    finally { parent?.Dispose(); }
});
try { return await updater.Run(); }
finally { monitorStop.Cancel(); await monitor; }

sealed record Options(string Mode, string Root, string Project, string Job, int Parent, string? Maps) {
    public static Options? Parse(string[] args) {
        var values = new Dictionary<string, string>();
        for (int i = 0; i < args.Length; i += 2) {
            if (i + 1 >= args.Length || !args[i].StartsWith("--") || !values.TryAdd(args[i], args[i + 1])) return null;
        }
        if (!values.TryGetValue("--mode", out var mode) || mode is not ("maps" or "schema" or "all" or "profile") ||
            !values.TryGetValue("--root", out var root) || !values.TryGetValue("--job", out var job)) return null;
        int parent = 0;
        if (values.TryGetValue("--parent", out var p) && (!int.TryParse(p, out parent) || parent < 0)) return null;
        return new(mode, Path.GetFullPath(root), values.TryGetValue("--project", out var project) ? Path.GetFullPath(project) : "",
            Path.GetFullPath(job), parent, values.GetValueOrDefault("--maps"));
    }
}

static class Files {
    public static string Hash(string path) { using var input = File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(input)); }
    public static void AtomicText(string path, string content) {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temp = path + ".tmp";
        File.WriteAllText(temp, content, new UTF8Encoding(false));
        for (int attempt = 0; ; attempt++) {
            try { File.Move(temp, path, true); break; }
            catch (IOException) when (attempt < 5) { Thread.Sleep(20); }
            catch (UnauthorizedAccessException) when (attempt < 5) { Thread.Sleep(20); }
        }
    }
    public static void AtomicCopy(string source, string destination) {
        if (Path.GetFullPath(source).Equals(Path.GetFullPath(destination), StringComparison.OrdinalIgnoreCase)) return;
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var temp = destination + ".tmp";
        File.Copy(source, temp, true);
        if (Hash(source) != Hash(temp)) throw new IOException("Copy verification failed: " + destination);
        for (int attempt = 0; ; attempt++) {
            try { File.Move(temp, destination, true); break; }
            catch (IOException) when (attempt < 5) { Thread.Sleep(20); }
            catch (UnauthorizedAccessException) when (attempt < 5) { Thread.Sleep(20); }
        }
    }
    public static bool Within(string path, string parent) => Path.GetFullPath(path).StartsWith(Path.GetFullPath(parent).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    public static void DeleteTree(string path, string parent) {
        if (!Within(path, parent)) throw new IOException("Refused cleanup outside update directory");
        if (Directory.Exists(path)) Directory.Delete(path, true);
    }
    // Keep the last successful dump until the replacement has passed validation.
    public static void PublishDirectory(string staged, string destination) {
        var parent = Path.GetDirectoryName(destination)!;
        if (!Within(staged, parent)) throw new IOException("Staging directory must be on the same volume and inside its destination parent");
        var previous = destination + ".previous";
        DeleteTree(previous, parent);
        if (Directory.Exists(destination)) Directory.Move(destination, previous);
        try { Directory.Move(staged, destination); }
        catch { if (Directory.Exists(previous) && !Directory.Exists(destination)) Directory.Move(previous, destination); throw; }
    }
}

static class GameLocation {
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder path, ref uint length);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static string? Executable(int pid) {
        var handle = OpenProcess(0x1000, false, pid); if (handle == IntPtr.Zero) return null;
        try { var buffer = new StringBuilder(32768); uint length = 32768; return QueryFullProcessImageName(handle, 0, buffer, ref length) ? buffer.ToString() : null; }
        finally { CloseHandle(handle); }
    }
    public static string? RunningGame() {
        foreach (var process in Process.GetProcessesByName("deadlock")) using (process) {
            var exe = Executable(process.Id);
            if (exe is not null) return Directory.GetParent(exe)?.Parent?.Parent?.FullName;
        }
        return null;
    }
    public static string FindMaps(string? supplied) {
        if (supplied is not null) {
            if (!Directory.Exists(supplied)) throw new DirectoryNotFoundException("Map directory does not exist: " + supplied);
            return Path.GetFullPath(supplied);
        }
        var game = RunningGame();
        if (game is not null && Directory.Exists(Path.Combine(game, "citadel", "maps"))) return Path.Combine(game, "citadel", "maps");
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var key in new[] { @"HKEY_CURRENT_USER\Software\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam" }) {
            foreach (var value in new[] { "SteamPath", "InstallPath" }) if (Registry.GetValue(key, value, null) is string path) roots.Add(path);
        }
        foreach (var root in roots.ToArray()) {
            var libraries = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (File.Exists(libraries)) foreach (Match match in Regex.Matches(File.ReadAllText(libraries), "\"path\"\\s+\"([^\"]+)\"")) roots.Add(match.Groups[1].Value.Replace("\\\\", "\\"));
        }
        foreach (var root in roots) {
            var maps = Path.Combine(root, "steamapps", "common", "Deadlock", "game", "citadel", "maps");
            if (Directory.Exists(maps)) return maps;
        }
        throw new DirectoryNotFoundException("Deadlock installation not found. Start Deadlock, then try again.");
    }
}

sealed class DataUpdater(Options options, CancellationToken cancellation) {
    readonly List<string> notes = [];
    int mapUpdated, mapCurrent, mapSkipped, mapFailed;
    bool dumpReady, readerReady, completedMaps;
    string readerIssue = "";
    string schemaDirectory = "";
    int progress;
    string Project => File.Exists(Path.Combine(options.Project, "CMakeLists.txt")) ? options.Project : options.Root;
    string CanonicalMaps => Path.Combine(Project, "maps");
    void Log(string line) { notes.Add(line); Console.WriteLine(line); Files.AtomicText(Path.Combine(options.Job, "report.txt"), string.Join(Environment.NewLine, notes)); }
    void Status(string state, string phase, string message, int percent) {
        progress = Math.Clamp(percent, 0, 100);
        string Clean(string text) => text.Replace('\r', ' ').Replace('\n', ' ');
        var content = $"state={state}\nphase={Clean(phase)}\nmessage={Clean(message)}\nprogress={progress}\nmap_updated={mapUpdated}\nmap_current={mapCurrent}\nmap_skipped={mapSkipped}\nmap_failed={mapFailed}\ndump_ready={(dumpReady ? 1 : 0)}\nreader_ready={(readerReady ? 1 : 0)}\nmaps_ready={(completedMaps ? 1 : 0)}\nschema_directory={Clean(schemaDirectory)}\n";
        Files.AtomicText(Path.Combine(options.Job, "status.ini"), content);
        Files.AtomicText(Path.Combine(options.Root, "data", "last-update.ini"), content);
    }
    public async Task<int> Run() {
        Directory.CreateDirectory(options.Job);
        // A second overlay or command-line invocation must not share dumper temp files.
        using var ownership = new Semaphore(1, 1, @"Local\DeadlockOverlayDataUpdate");
        if (!ownership.WaitOne(0)) { Status("failed", "Busy", "Another data update is already running.", 0); return 1; }
        try {
            try { Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.BelowNormal; } catch { }
            Status("running", "Discovering game", "Finding installed maps and game binaries...", 0);
            cancellation.ThrowIfCancellationRequested();
            var maps = GameLocation.FindMaps(options.Maps);
            Log("Started " + DateTimeOffset.Now.ToString("O"));
            Log("Installed maps: " + maps);
            Log("Project data: " + Project);
            if (options.Mode is "maps" or "all") InstallMaps(maps);
            if (options.Mode is "schema" or "all") {
                try { await DumpSchema(maps); }
                catch (OperationCanceledException) { throw; }
                catch (Exception e) {
                    Log("Schema update failed: " + e.Message);
                    Status("failed", "Schema update failed", (completedMaps ? "Maps ready. " : "") + e.Message, progress);
                    return 1;
                }
            }
            if (options.Mode == "profile") {
                schemaDirectory = Path.Combine(Project, "data", "schema-dump", "deadlock");
                dumpReady = Directory.Exists(schemaDirectory);
                var game = Directory.GetParent(maps)?.Parent?.FullName ?? throw new IOException("Invalid game directory");
                await UpdateReaderProfile(game);
            }
            cancellation.ThrowIfCancellationRequested();
            string message = options.Mode == "maps" ? $"Maps ready: {mapUpdated} installed, {mapCurrent} current, {mapSkipped} without physics."
                : !readerReady ? "Data saved. Reader profile not installed: " + readerIssue
                : "Game data and reader profile updated. Reader reloads automatically.";
            if (options.Mode == "schema" && readerReady) message = "Schema and reader profile updated. Reader reloads automatically.";
            if (mapFailed > 0) message += $" {mapFailed} map export(s) failed; see report.";
            Log(message);
            Status(mapFailed > 0 || (options.Mode != "maps" && !readerReady) ? "warnings" : "complete", "Finished", message, 100);
            return mapFailed > 0 || (options.Mode != "maps" && !readerReady) ? 1 : 0;
        } catch (OperationCanceledException) { Log("Update cancelled; previously published data retained."); Status("cancelled", "Cancelled", "Update cancelled. Successfully installed maps are retained.", progress); return 3; }
        catch (Exception e) { Log(e.ToString()); Status("failed", "Update failed", e.Message, progress); return 1; }
        finally { ownership.Release(); }
    }
    static string MapName(string archive) {
        var name = Path.GetFileNameWithoutExtension(archive);
        if (name.EndsWith("_dir", StringComparison.OrdinalIgnoreCase)) name = name[..^4];
        if (!Regex.IsMatch(name, "^[A-Za-z0-9_-]{1,96}$")) throw new InvalidDataException("Unsupported map name: " + name);
        return name.ToLowerInvariant();
    }
    public static bool CurrentMesh(string mesh, string sourceHash) {
        try {
            using var metadata = JsonDocument.Parse(File.ReadAllText(mesh + ".json")); var data = metadata.RootElement;
            long triangles = data.GetProperty("triangles").GetInt64();
            return triangles is > 0 and <= 8000000 && data.GetProperty("exporterVersion").GetInt32() == MapExporter.FormatVersion &&
                data.GetProperty("sourceSha256").GetString() == sourceHash && new FileInfo(mesh).Length == triangles * 36 &&
                data.GetProperty("meshSha256").GetString() == Files.Hash(mesh);
        } catch { return false; }
    }
    void Unavailable(string name, string reason) {
        foreach (var directory in new[] { CanonicalMaps, Path.Combine(options.Root, "maps") }.Distinct(StringComparer.OrdinalIgnoreCase)) {
            var mesh = Path.Combine(directory, name + ".tri");
            if (File.Exists(mesh)) Files.AtomicText(mesh + ".unavailable", reason);
        }
    }
    void InstallMaps(string maps) {
        var archives = Directory.GetFiles(maps, "*.vpk", SearchOption.AllDirectories)
            .Where(path => !Regex.IsMatch(Path.GetFileNameWithoutExtension(path), @"_\d{3}$"))
            .Order(StringComparer.OrdinalIgnoreCase).ToArray();
        if (archives.Length == 0) throw new InvalidDataException("No map VPKs were found.");
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var staged = Path.Combine(Project, "data", "map-staging", Path.GetFileName(options.Job));
        Directory.CreateDirectory(staged);
        try {
            for (int i = 0; i < archives.Length; i++) {
                cancellation.ThrowIfCancellationRequested();
                var archive = archives[i]; var name = MapName(archive);
                Status("running", $"Maps {i + 1}/{archives.Length}", "Checking " + name + "...", i * (options.Mode == "all" ? 55 : 95) / archives.Length);
                var mesh = Path.Combine(CanonicalMaps, name + ".tri");
                try {
                    var hash = Files.Hash(archive);
                    if (CurrentMesh(mesh, hash)) {
                        if (!seen.Add(name)) throw new InvalidDataException("Duplicate collision-map name requires review: " + archive);
                        mapCurrent++; Log(name + ": unchanged, verified existing mesh.");
                    }
                    else {
                        Status("running", $"Maps {i + 1}/{archives.Length}", "Exporting " + name + "...", progress);
                        MapExporter.Export(archive, staged, cancellation);
                        var exported = Path.Combine(staged, Path.GetFileNameWithoutExtension(archive) + ".tri");
                        if (!CurrentMesh(exported, hash)) throw new InvalidDataException("Export validation failed, or the VPK changed during extraction.");
                        if (!seen.Add(name)) throw new InvalidDataException("Duplicate collision-map name requires review: " + archive);
                        cancellation.ThrowIfCancellationRequested();
                        Files.AtomicCopy(exported, mesh);
                        Files.AtomicCopy(exported + ".json", mesh + ".json");
                        mapUpdated++; Log(name + ": exported and verified.");
                    }
                    Files.AtomicCopy(mesh, Path.Combine(options.Root, "maps", name + ".tri"));
                    Files.AtomicCopy(mesh + ".json", Path.Combine(options.Root, "maps", name + ".tri.json"));
                    File.Delete(mesh + ".unavailable");
                    File.Delete(Path.Combine(options.Root, "maps", name + ".tri.unavailable"));
                } catch (OperationCanceledException) { throw; }
                catch (InvalidDataException e) when (e.Message is "No physics aggregate" or "World physics entry missing" or "No solid collision triangles extracted") {
                    mapSkipped++; Log(name + ": no usable collision geometry (skipped). " + e.Message);
                    if (!seen.Contains(name)) Unavailable(name, e.Message);
                } catch (Exception e) { mapFailed++; Log(name + ": FAILED: " + e.Message); Unavailable(name, e.Message); }
            }
            completedMaps = true;
        } finally { Files.DeleteTree(staged, Path.Combine(Project, "data", "map-staging")); }
    }
    async Task DumpSchema(string maps) {
        if (GameLocation.RunningGame() is null) throw new InvalidOperationException("Start Deadlock and load its main menu before dumping schema.");
        using (var identity = WindowsIdentity.GetCurrent()) if (!new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator)) throw new InvalidOperationException("Schema dumping needs Windows administrator approval.");
        var game = Directory.GetParent(maps)?.Parent?.FullName ?? throw new IOException("Invalid map directory");
        var client = Path.Combine(game, "citadel", "bin", "win64", "client.dll");
        var engine = Path.Combine(game, "bin", "win64", "engine2.dll");
        var clientHash = Files.Hash(client); var engineHash = Files.Hash(engine);
        var toolDirectory = Path.Combine(options.Root, "tools", "schema-dumper");
        var tool = Path.Combine(toolDirectory, "dezlock-dump.exe");
        if (!File.Exists(tool) || !File.Exists(Path.Combine(toolDirectory, "dezlock-worker.dll"))) throw new FileNotFoundException("Bundled dumper is missing. Rebuild the data tools.");
        var parent = Path.Combine(Project, "data", "schema-dump");
        schemaDirectory = Path.Combine(parent, "deadlock");
        var staged = Path.Combine(parent, ".staging-" + Path.GetFileName(options.Job));
        Directory.CreateDirectory(staged);
        try {
            Status("running", "Dumping schema", "Reading schema and generating SDK/signatures...", options.Mode == "all" ? 60 : 10);
            var info = new ProcessStartInfo(tool) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = toolDirectory, RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true };
            foreach (var arg in new[] { "--process", "deadlock.exe", "--output", staged, "--all", "--no-update-check", "--wait", "300" }) info.ArgumentList.Add(arg);
            using var process = Process.Start(info) ?? throw new IOException("Could not start the schema dumper.");
            try { process.PriorityClass = ProcessPriorityClass.BelowNormal; } catch { }
            process.StandardInput.Close(); // Upstream final ReadConsoleInput returns immediately on a pipe.
            var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation); timeout.CancelAfter(TimeSpan.FromMinutes(10));
            try { await process.WaitForExitAsync(timeout.Token); }
            catch (OperationCanceledException) {
                if (!process.HasExited) process.Kill(true);
                await process.WaitForExitAsync();
                File.WriteAllText(Path.Combine(options.Job, "dumper.log"), await stdout + Environment.NewLine + await stderr);
                cancellation.ThrowIfCancellationRequested();
                throw new TimeoutException("Schema dumper timed out after 10 minutes; the previous dump was retained.");
            }
            File.WriteAllText(Path.Combine(options.Job, "dumper.log"), await stdout + Environment.NewLine + await stderr);
            if (process.ExitCode != 0) throw new IOException($"Schema dumper returned {process.ExitCode}. See dumper.log in the update report folder.");
            cancellation.ThrowIfCancellationRequested();
            Status("running", "Validating schema", "Checking field counts, required layouts and binary hashes...", 92);
            var validation = ReaderSupport.Validate(staged, clientHash, engineHash);
            if (Files.Hash(client) != clientHash || Files.Hash(engine) != engineHash) throw new InvalidDataException("The game binaries changed during the dump. Try again after the game update finishes.");
            Files.AtomicText(Path.Combine(staged, "overlay-validation.json"), JsonSerializer.Serialize(validation, new JsonSerializerOptions { WriteIndented = true }));
            Log($"Schema fields: {validation.Fields}; client SHA256: {clientHash}");
            cancellation.ThrowIfCancellationRequested();
            Files.PublishDirectory(staged, schemaDirectory);
            dumpReady = true;
            Log("Schema saved: " + schemaDirectory + "; previous version retained in deadlock.previous.");
            await UpdateReaderProfile(game);
        } finally { Files.DeleteTree(staged, parent); }
    }
    async Task UpdateReaderProfile(string game) {
            try {
                var client = Path.Combine(game, "citadel", "bin", "win64", "client.dll");
                var engine = Path.Combine(game, "bin", "win64", "engine2.dll");
                var world = Path.Combine(game, "bin", "win64", "worldrenderer.dll");
                var validation = JsonSerializer.Deserialize<SchemaValidation>(File.ReadAllText(Path.Combine(schemaDirectory, "overlay-validation.json"))) ?? throw new InvalidDataException("Missing schema validation");
                Status("running", "Resolving reader profile", "Reading new field offsets, globals and private-layout witnesses...", 94);
                var candidate = ReaderProfiles.Create(schemaDirectory, client, engine, world, options.Job, Log, cancellation);
                Status("running", "Validating reader profile", "Checking projection, entities, bones and hitboxes in the running game...", 97);
                await ReaderProfiles.Verify(candidate, options.Root, options.Job, Log, cancellation);
                if (Files.Hash(client) != candidate.ClientHash || Files.Hash(engine) != candidate.EngineHash || Files.Hash(world) != candidate.WorldHash) throw new InvalidDataException("DLLs changed before profile publication");
                cancellation.ThrowIfCancellationRequested();
                ReaderProfiles.Publish(candidate, options.Root, Project, Log);readerReady = true;
                Files.AtomicText(Path.Combine(schemaDirectory, "overlay-validation.json"), JsonSerializer.Serialize(validation with { ReaderCompatible = true, Differences = [] }, new JsonSerializerOptions { WriteIndented = true }));
            } catch (OperationCanceledException) { throw; }
            catch (Exception e) { readerReady = false;readerIssue = e.Message;Log("Reader profile not installed: " + e.Message); }
    }
}
