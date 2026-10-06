using System.Text.RegularExpressions;

sealed record SchemaValidation(string ClientSha256, string EngineSha256, int Fields, bool ReaderCompatible, string[] Differences);

static class ReaderSupport {
    // This whitelist describes the manually validated reader, not every layout in a generated SDK.
    // Unknown binary hashes remain unsupported even when these schema fields match.
    public const string ClientHash = "678AEC94ADB44623E335EE7EC76C08F4477A0EA88A85CEA5CB70ABACE1BACAA8";
    public const string EngineHash = "AAC84E48DE57844D5499AF8FD95C976143EFE2F14845FF2409B111EB9FF5CE74";
    public static readonly (string Class, string Field, uint Offset)[] Required = [
        ("C_BaseEntity", "m_pGameSceneNode", 0x330),
        ("C_BaseEntity", "m_pCollision", 0x340),
        ("C_BaseEntity", "m_iMaxHealth", 0x350),
        ("C_BaseEntity", "m_iHealth", 0x354),
        ("C_BaseEntity", "m_lifeState", 0x35c),
        ("C_BaseEntity", "m_iTeamNum", 0x3ef),
        ("CBasePlayerController", "m_hPawn", 0x6bc),
        ("CBasePlayerController", "m_bIsLocalPlayerController", 0x790),
        ("CGameSceneNode", "m_vecAbsOrigin", 0xc8),
        ("CGameSceneNode", "m_bDormant", 0x103),
        ("CSkeletonInstance", "m_modelState", 0x140),
        ("CCollisionProperty", "m_vecMins", 0x40),
        ("CCollisionProperty", "m_vecMaxs", 0x4c)
    ];
    public static SchemaValidation Validate(string dump, string client, string engine) {
        var sdk = Path.Combine(dump, "sdk");
        var json = Path.Combine(dump, "_all-modules.json");
        if (!File.Exists(json) || new FileInfo(json).Length < 1024 || !Directory.Exists(sdk)) throw new InvalidDataException("Dump is missing its schema JSON or generated SDK.");
        int fields = 0; var offsets = new Dictionary<string, uint>();
        foreach (var file in Directory.GetFiles(sdk, "_offsets.hpp", SearchOption.AllDirectories)) {
            bool clientModule = string.Equals(Path.GetFileName(Path.GetDirectoryName(file)), "client", StringComparison.OrdinalIgnoreCase);
            string currentClass = "";
            foreach (var line in File.ReadLines(file)) {
                var ns = Regex.Match(line, @"^namespace (\w+) \{"); if (ns.Success) currentClass = ns.Groups[1].Value;
                var field = Regex.Match(line, @"^\s+constexpr uint32_t (\w+) = 0x([\da-fA-F]+);");
                if (!field.Success) continue;
                fields++;
                if (clientModule) offsets[currentClass + "::" + field.Groups[1].Value] = Convert.ToUInt32(field.Groups[2].Value, 16);
            }
        }
        if (fields < 1000 || offsets.Count < 500) throw new InvalidDataException($"Schema dump is incomplete ({fields} fields, {offsets.Count} client fields). The previous dump was retained.");
        var differences = new List<string>();
        foreach (var expected in Required) {
            var name = expected.Class + "::" + expected.Field;
            if (!offsets.TryGetValue(name, out var value)) differences.Add(name + " is missing");
            else if (value != expected.Offset) differences.Add($"{name}: expected 0x{expected.Offset:X}, got 0x{value:X}");
        }
        if (!client.Equals(ClientHash, StringComparison.OrdinalIgnoreCase)) differences.Add("New client.dll hash: code signatures, globals and private layouts require review.");
        if (!engine.Equals(EngineHash, StringComparison.OrdinalIgnoreCase)) differences.Add("New engine2.dll hash: replay/map reader addresses require review.");
        return new(client, engine, fields, differences.Count == 0, differences.ToArray());
    }
}
