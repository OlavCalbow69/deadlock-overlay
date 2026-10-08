using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

sealed record ReaderCandidate(string Path, string ClientHash, string EngineHash, string WorldHash, string Manifest, Dictionary<string, uint> Values);

static class ReaderProfiles {
    sealed record Recipe(string Pattern, int Displacement, int End);
    sealed record Symbol(string Key, string Module, Recipe[] Recipes);
    sealed record Vtable(string Key, string Module, string Type);
    sealed record Witness(string Key, string Module, string Pattern);
    sealed record Vfunc(string Key, string Module, string Vtable, int Slot, string Pattern);
    sealed record Manifest(int Version, string Layout, Symbol[] Symbols, Vtable[] Vtables, Witness[] Witnesses, Vfunc[] Vfuncs);
    static string Resource(string name) {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name) ?? throw new InvalidDataException("Missing profile resolver resource: " + name);
        using var reader = new StreamReader(stream); return reader.ReadToEnd().Replace("\r\n", "\n");
    }
    public static ReaderCandidate Create(string dump, string client, string engine, string world, string job, Action<string> log, CancellationToken cancel) {
        var clientHash = Files.Hash(client); var engineHash = Files.Hash(engine); var worldHash = Files.Hash(world);
        // Bind the dump to the exact files it was produced from, not just similarly named SDK headers.
        var validation = JsonSerializer.Deserialize<SchemaValidation>(File.ReadAllText(Path.Combine(dump, "overlay-validation.json"))) ?? throw new InvalidDataException("Missing dump identity");
        if (validation.ClientSha256 != clientHash || validation.EngineSha256 != engineHash) throw new InvalidDataException("Schema dump belongs to a different DLL build. Run Dump schema first.");
        string contract = Resource("ReaderContract"), signatures = Resource("ReaderSignatures");
        var manifest = JsonSerializer.Deserialize<Manifest>(signatures, new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? throw new InvalidDataException("Invalid resolver manifest");
        if (manifest.Version != 1 || manifest.Layout != "source2-6759-v1") throw new InvalidDataException("Unsupported resolver version");
        string manifestHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(contract + signatures))).ToLowerInvariant();
        var offsets = new Dictionary<string, uint>();var representations = new Dictionary<string,(string Type,uint Width)>(); string cls = "";
        foreach (var line in File.ReadLines(Path.Combine(dump, "sdk", "client", "_offsets.hpp"))) {
            var ns = Regex.Match(line, @"^namespace (\w+) \{"); if (ns.Success) cls = ns.Groups[1].Value;
            var field = Regex.Match(line, @"^\s+constexpr uint32_t (\w+) = 0x([\da-fA-F]+);");
            if (field.Success) {
                string name = cls + "::" + field.Groups[1].Value;offsets.Add(name, Convert.ToUInt32(field.Groups[2].Value, 16));
                var repr = Regex.Match(line, @"//\s*(.*?)\s*\((\d+)b\)");
                if (repr.Success) representations.Add(name,(repr.Groups[1].Value,uint.Parse(repr.Groups[2].Value,CultureInfo.InvariantCulture)));
            }
        }
        var values = new Dictionary<string, uint>();
        foreach (Match f in Regex.Matches(contract, "PROFILE_FIELD\\((\\w+),(0x[\\da-fA-F]+),\"([^\"]+)\",\"([^\"]+)\"\\)")) {
            string name = f.Groups[3].Value + "::" + f.Groups[4].Value;
            if (!offsets.TryGetValue(name, out uint value) || value >= 0x20000) throw new InvalidDataException("Required schema field unavailable: " + name);
            string key=f.Groups[1].Value;
            uint width=key switch {
                "scene_node" or "collision" or "modifier_property" or "model_handle" or "mesh_mask" or "hero_sort" or "hero_search"=>8,
                "max_health" or "health" or "pawn_handle" or "disabled_groups" or "hero_unspawned" or "spawn_id" or "hero_id" or "sniper_scope" or "match_mode" or "game_mode"=>4,
                "life_state" or "team" or "local_controller" or "dormant" or "hitbox_set"=>1,
                "node_world"=>32,"hero_spawned" or "hero_loading"=>16,"abilities"=>24,"weapon_map"=>40,_=>0
            };
            string primitive=key switch {"max_health" or "health"=>"int32","life_state" or "team" or "hitbox_set"=>"uint8","local_controller" or "dormant"=>"bool","sniper_scope"=>"GameTime_t",_=>""};
            if(width!=0&&(!representations.TryGetValue(name,out var repr)||repr.Width!=width||(!string.IsNullOrEmpty(primitive)&&repr.Type!=primitive)))
                throw new InvalidDataException("Schema field representation changed: "+name);
            values.Add("field." + f.Groups[1].Value, value);
            if (value != Convert.ToUInt32(f.Groups[2].Value[2..], 16)) log($"Updated schema field {name}: 0x{value:X}");
        }
        var images = new Dictionary<string, PeImage> { ["client"] = new(client), ["engine"] = new(engine), ["world"] = new(world) };
        foreach (var symbol in manifest.Symbols) {
            cancel.ThrowIfCancellationRequested(); var image = images[symbol.Module]; var targets = new HashSet<uint>();
            foreach (var recipe in symbol.Recipes) {
                var matches = image.Matches(recipe.Pattern); if (matches.Length != 1) continue;
                if (recipe.Displacement < 0 || recipe.End <= recipe.Displacement || recipe.End > recipe.Pattern.Split(' ').Length) throw new InvalidDataException("Invalid RIP resolver");
                int displacement = BinaryPrimitives.ReadInt32LittleEndian(image.At(matches[0] + (uint)recipe.Displacement, 4));
                long target = (long)matches[0] + recipe.End + displacement;
                if (target < 0x1000 || target >= image.Size || !image.Data((uint)target)) throw new InvalidDataException("Resolved global is outside DLL data: " + symbol.Key);
                targets.Add((uint)target);
            }
            if (targets.Count != 1) throw new InvalidDataException($"{symbol.Key}: signature missing, ambiguous or conflicting; reader profile needs review");
            uint address = targets.Single(); values.Add("address." + symbol.Key, address); log($"Resolved {symbol.Module}.{symbol.Key}: 0x{address:X}");
        }
        foreach (var table in manifest.Vtables) {
            cancel.ThrowIfCancellationRequested(); uint address = images[table.Module].Vtable(table.Type);
            values.Add("address." + table.Key, address); log($"Validated {table.Module}.{table.Key}: 0x{address:X}");
        }
        foreach (var witness in manifest.Witnesses) {
            cancel.ThrowIfCancellationRequested(); var matches = images[witness.Module].Matches(witness.Pattern);
            if (matches.Length != 1) throw new InvalidDataException($"Private layout {witness.Key}: expected one unchanged code witness, found {matches.Length}; reader review required");
            log($"Private layout {witness.Key}: verified at 0x{matches[0]:X}");
        }
        foreach (var witness in manifest.Vfuncs) {
            cancel.ThrowIfCancellationRequested();var image = images[witness.Module];
            uint function = image.VirtualFunction(values["address." + witness.Vtable], witness.Slot);
            if (!image.MatchesAt(function, witness.Pattern)) throw new InvalidDataException("Private virtual layout " + witness.Key + " changed; reader review required");
            log($"Private virtual layout {witness.Key}: verified at 0x{function:X}");
        }
        // Every address in the C++ contract must be generated; no missing value may fall back to an old RVA.
        foreach (Match a in Regex.Matches(contract, "PROFILE_ADDRESS\\((\\w+),")) if (!values.ContainsKey("address." + a.Groups[1].Value)) throw new InvalidDataException("Unresolved address: " + a.Groups[1].Value);
        if (Files.Hash(client) != clientHash || Files.Hash(engine) != engineHash || Files.Hash(world) != worldHash) throw new InvalidDataException("DLLs changed while resolving the profile");
        var content = new StringBuilder().AppendLine("format=1").AppendLine("layout=" + manifest.Layout).AppendLine("manifest=" + manifestHash)
            .AppendLine("validation=passed").AppendLine("client_hash=" + clientHash.ToLowerInvariant()).AppendLine("engine_hash=" + engineHash.ToLowerInvariant()).AppendLine("world_hash=" + worldHash.ToLowerInvariant());
        foreach (var pair in values.OrderBy(p => p.Key, StringComparer.Ordinal)) content.AppendLine(pair.Key + "=0x" + pair.Value.ToString("X", CultureInfo.InvariantCulture));
        var path = Path.Combine(job, "candidate-reader-profile.ini"); Files.AtomicText(path, content.ToString());
        return new(path, clientHash, engineHash, worldHash, manifestHash, values);
    }
    public static async Task Verify(ReaderCandidate candidate, string root, string job, Action<string> log, CancellationToken cancel) {
        var executable = Path.Combine(root, "DeadlockOverlay.exe"); var result = Path.Combine(job, "profile-live-validation.json");
        // A repeated job must never accept the previous attempt's success report.
        if (File.Exists(result)) File.Delete(result);
        var info = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = root, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var arg in new[] { "--validate-profile", candidate.Path, "--profile-result", result }) info.ArgumentList.Add(arg);
        using var process = Process.Start(info) ?? throw new IOException("Could not start the read-only profile validator");
        var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancel); timeout.CancelAfter(TimeSpan.FromSeconds(30));
        try { await process.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException) { if (!process.HasExited) process.Kill(true); await process.WaitForExitAsync(); cancel.ThrowIfCancellationRequested(); throw new TimeoutException("Live profile validation timed out"); }
        Files.AtomicText(Path.Combine(job, "profile-validator.log"), await stdout + Environment.NewLine + await stderr);
        if (!File.Exists(result)) throw new InvalidDataException("Overlay does not support this resolver version; rebuild/install the updated EXE first");
        using var report = JsonDocument.Parse(File.ReadAllText(result));
        if (process.ExitCode != 0 || !report.RootElement.GetProperty("passed").GetBoolean()) throw new InvalidDataException(report.RootElement.GetProperty("message").GetString() ?? "Live reader validation failed");
        log("Read-only live validation passed: projection, entity handles, bones, hitboxes and hero/weapon metadata.");
    }
    public static void Publish(ReaderCandidate candidate, string root, string project, Action<string> log) {
        var filename = candidate.ClientHash.ToLowerInvariant() + ".ini";
        foreach (var destination in new[] { Path.Combine(root, "data", "reader-profiles", filename), Path.Combine(project, "data", "reader-profiles", filename) }.Distinct(StringComparer.OrdinalIgnoreCase)) {
            if (File.Exists(destination)) Files.AtomicCopy(destination, destination + ".previous");
            Files.AtomicCopy(candidate.Path, destination); log("Installed reader profile: " + destination);
        }
    }
}
