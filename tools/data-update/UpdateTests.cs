using System.Text.Json;

static class UpdateTests {
    public static int Run() {
        var root = Path.Combine(Path.GetTempPath(), "DeadlockUpdateTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        int checks = 0;
        void Check(bool value, string message) { checks++; if (!value) throw new Exception(message); }
        try {
            Check(Options.Parse(["--mode", "wrong", "--root", root, "--job", root]) is null, "Reject invalid mode");
            Check(Options.Parse(["--mode", "maps", "--root", root, "--job"]) is null, "Reject missing argument");
            Check(Options.Parse(["--mode", "maps", "--root", root, "--job", root]) is not null, "Accept maps-only job");
            Check(!Files.Within(root, root), "Cleanup cannot delete its root");
            Check(!Files.Within(root + "-other", root), "Cleanup cannot escape to sibling");
            var mesh = Path.Combine(root, "test.tri"); File.WriteAllBytes(mesh, new byte[36]);
            var metadata = new { triangles = 1, exporterVersion = MapExporter.FormatVersion, sourceSha256 = "abc", meshSha256 = Files.Hash(mesh) };
            Files.AtomicText(mesh + ".json", JsonSerializer.Serialize(metadata));
            Check(DataUpdater.CurrentMesh(mesh, "abc"), "Accept matching export fingerprint");
            Check(!DataUpdater.CurrentMesh(mesh, "changed"), "A changed VPK forces re-export");
            File.WriteAllBytes(mesh, new byte[72]);
            Check(!DataUpdater.CurrentMesh(mesh, "abc"), "Corrupted mesh is never reused");
            var copied = Path.Combine(root, "installed.tri"); Files.AtomicCopy(mesh, copied);
            Check(Files.Hash(mesh) == Files.Hash(copied), "Published file matches source");
            var old = Path.Combine(root, "dump"); Directory.CreateDirectory(old); File.WriteAllText(Path.Combine(old, "old"), "good");
            try { Files.PublishDirectory(Path.Combine(root, "missing"), old); Check(false, "Expected publish failure"); } catch (DirectoryNotFoundException) { }
            Check(File.Exists(Path.Combine(old, "old")), "Failed publication restores previous dump");
            var staged = Path.Combine(root, "stage"); Directory.CreateDirectory(staged); File.WriteAllText(Path.Combine(staged, "new"), "good");
            Files.PublishDirectory(staged, old);
            Check(File.Exists(Path.Combine(old, "new")) && File.Exists(Path.Combine(old + ".previous", "old")), "Successful publication retains backup");
            var sdk = Path.Combine(root, "sdk", "client"); Directory.CreateDirectory(sdk);
            File.WriteAllText(Path.Combine(root, "_all-modules.json"), new string(' ', 1024));
            var header = new System.Text.StringBuilder();
            foreach (var expected in ReaderSupport.Required) header.AppendLine($"namespace {expected.Class} {{").AppendLine($"    constexpr uint32_t {expected.Field} = 0x{expected.Offset:X};");
            header.AppendLine("namespace filler {");
            for (int i = 0; i < 1100; i++) header.AppendLine($"    constexpr uint32_t field{i} = 0x10;");
            var offsets = Path.Combine(sdk, "_offsets.hpp"); File.WriteAllText(offsets, header.ToString());
            Check(ReaderSupport.Validate(root, ReaderSupport.ClientHash, ReaderSupport.EngineHash).ReaderCompatible, "Known binary and matching schema are accepted");
            Check(!ReaderSupport.Validate(root, "new client", ReaderSupport.EngineHash).ReaderCompatible, "Matching fields do not approve unknown binary code");
            File.WriteAllText(offsets, header.ToString().Replace("m_iHealth = 0x354", "m_iHealth = 0x400"));
            Check(!ReaderSupport.Validate(root, ReaderSupport.ClientHash, ReaderSupport.EngineHash).ReaderCompatible, "Changed required offset is reported");
            File.WriteAllText(offsets, "");
            try { ReaderSupport.Validate(root, ReaderSupport.ClientHash, ReaderSupport.EngineHash); Check(false, "Expected empty dump rejection"); } catch (InvalidDataException) { }
            Check(true, "Empty dump rejected");
            Console.WriteLine($"Data updater checks passed: {checks}"); return 0;
        } catch (Exception e) { Console.Error.WriteLine(e); return 1; }
        finally { Files.DeleteTree(root, Path.GetTempPath()); }
    }
}
