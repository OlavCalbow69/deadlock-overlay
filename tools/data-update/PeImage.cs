using System.Buffers.Binary;
using System.Text;

sealed class PeImage {
    public sealed record Section(uint Rva, uint VirtualSize, int Offset, int Size, uint Flags);
    public byte[] Bytes { get; }
    public ulong Base { get; }
    public uint Size { get; }
    public Section[] Sections { get; }
    public PeImage(string path) {
        Bytes = File.ReadAllBytes(path);
        if (Bytes.Length < 256 || U16(0) != 0x5a4d) throw new InvalidDataException("Invalid DLL: " + path);
        int nt = checked((int)U32(60));
        if (U32(nt) != 0x4550 || U16(nt + 4) != 0x8664 || U16(nt + 24) != 0x20b) throw new InvalidDataException("Expected an x64 PE DLL");
        Base = U64(nt + 48); Size = U32(nt + 80);
        int count = U16(nt + 6), table = nt + 24 + U16(nt + 20);
        if (count is < 1 or > 96 || Size is < 4096 or > 0x40000000) throw new InvalidDataException("Invalid PE sections");
        var sections = new List<Section>();
        for (int i = 0; i < count; i++) {
            int at = checked(table + i * 40); uint virtualSize = U32(at + 8), rva = U32(at + 12);
            int size = checked((int)U32(at + 16)), offset = checked((int)U32(at + 20)); uint flags = U32(at + 36);
            if (offset < 0 || size < 0 || (long)offset + size > Bytes.Length || (ulong)rva + Math.Max(virtualSize, (uint)size) > Size) throw new InvalidDataException("PE section is outside its image");
            sections.Add(new(rva, virtualSize, offset, size, flags));
        }
        Sections = sections.ToArray();
    }
    ushort U16(int at) => BinaryPrimitives.ReadUInt16LittleEndian(Bytes.AsSpan(at, 2));
    uint U32(int at) => BinaryPrimitives.ReadUInt32LittleEndian(Bytes.AsSpan(at, 4));
    ulong U64(int at) => BinaryPrimitives.ReadUInt64LittleEndian(Bytes.AsSpan(at, 8));
    public ReadOnlySpan<byte> At(uint rva, int length) {
        foreach (var s in Sections) if (rva >= s.Rva && (ulong)rva + (uint)length <= (ulong)s.Rva + (uint)s.Size)
            return Bytes.AsSpan(checked(s.Offset + (int)(rva - s.Rva)), length);
        throw new InvalidDataException($"DLL RVA 0x{rva:X} is outside file-backed sections");
    }
    public bool Executable(uint rva) => Sections.Any(s => (s.Flags & 0x20000000) != 0 && rva >= s.Rva && (ulong)rva < (ulong)s.Rva + (uint)s.Size);
    public bool Data(uint rva) => Sections.Any(s => (s.Flags & 0x20000000) == 0 && rva >= s.Rva && (ulong)rva < (ulong)s.Rva + Math.Max(s.VirtualSize, (uint)s.Size));
    public bool MatchesAt(uint rva, string text) {
        var tokens = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);var bytes = At(rva, tokens.Length);
        for (int i = 0; i < tokens.Length; i++) if (tokens[i] != "?" && bytes[i] != Convert.ToByte(tokens[i], 16)) return false;
        return true;
    }
    public uint VirtualFunction(uint table, int slot) {
        if (slot is < 0 or > 255) throw new InvalidDataException("Invalid virtual function slot");
        ulong address = BinaryPrimitives.ReadUInt64LittleEndian(At(table + (uint)slot * 8, 8));
        if (address < Base || address - Base >= Size || !Executable((uint)(address - Base))) throw new InvalidDataException("Vtable function is outside DLL code");
        return (uint)(address - Base);
    }
    public uint[] Matches(string text) {
        var pattern = text.Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(t => t == "?" ? -1 : Convert.ToInt32(t, 16)).ToArray();
        if (pattern.Length is < 8 or > 4096 || pattern.Any(n => n is < -1 or > 255)) throw new InvalidDataException("Invalid resolver pattern");
        int best = 0, start = 0;
        for (int i = 0; i < pattern.Length;) {
            if (pattern[i] < 0) { i++; continue; }
            int at = i; while (i < pattern.Length && pattern[i] >= 0) i++;
            if (i - at > best) { best = i - at; start = at; }
        }
        if (best < 6) throw new InvalidDataException("Resolver pattern is too broad");
        var anchor = pattern.Skip(start).Take(best).Select(n => (byte)n).ToArray(); var result = new List<uint>();
        foreach (var s in Sections.Where(s => (s.Flags & 0x20000000) != 0)) {
            var bytes = Bytes.AsSpan(s.Offset, s.Size); int offset = 0;
            while (offset < bytes.Length) {
                int found = bytes[offset..].IndexOf(anchor); if (found < 0) break;
                found += offset; offset = found + 1; int candidate = found - start;
                if (candidate < 0 || candidate + pattern.Length > bytes.Length) continue;
                bool match = true;
                for (int i = 0; i < pattern.Length; i++) if (pattern[i] >= 0 && bytes[candidate + i] != pattern[i]) { match = false; break; }
                if (match) { result.Add(s.Rva + (uint)candidate); if (result.Count > 16) return result.ToArray(); }
            }
        }
        return result.ToArray();
    }
    // EModifierValue registration records contain name/value pairs at a 0x20
    // stride. Verify both neighboring records so an unrelated name reference
    // cannot be mistaken for the enum's numeric value.
    public uint ModifierValue(string name) {
        const string prefix = "MODIFIER_VALUE_";
        if (!name.StartsWith(prefix, StringComparison.Ordinal)) throw new InvalidDataException("Invalid modifier name");
        var needle = Encoding.ASCII.GetBytes(name + "\0");var prefixBytes = Encoding.ASCII.GetBytes(prefix);
        var names = new HashSet<uint>();var values = new HashSet<uint>();
        foreach (var s in Sections.Where(s => (s.Flags & 0x20000000) == 0)) {
            var bytes = Bytes.AsSpan(s.Offset, s.Size);int offset = 0;
            while (offset < bytes.Length) {
                int found = bytes[offset..].IndexOf(needle);if (found < 0) break;found += offset;offset = found + 1;names.Add(s.Rva + (uint)found);
            }
        }
        bool Neighbor(uint address, ulong expected) {
            try {
                var entry = At(address, 16);ulong pointer = BinaryPrimitives.ReadUInt64LittleEndian(entry);
                return BinaryPrimitives.ReadUInt64LittleEndian(entry[8..]) == expected && pointer >= Base && pointer - Base < Size
                    && Data((uint)(pointer - Base)) && At((uint)(pointer - Base), prefixBytes.Length).SequenceEqual(prefixBytes);
            } catch (InvalidDataException) { return false; }
        }
        foreach (uint nameRva in names) {
            var pointer = new byte[8];BinaryPrimitives.WriteUInt64LittleEndian(pointer, Base + nameRva);
            foreach (var s in Sections.Where(s => (s.Flags & 0x20000000) == 0)) {
                var bytes = Bytes.AsSpan(s.Offset, s.Size);int offset = 0;
                while (offset < bytes.Length) {
                    int found = bytes[offset..].IndexOf(pointer);if (found < 0) break;found += offset;offset = found + 1;
                    uint at = s.Rva + (uint)found;if ((at & 7) != 0 || found + 16 > bytes.Length || at < 32) continue;
                    ulong value = BinaryPrimitives.ReadUInt64LittleEndian(bytes[(found + 8)..]);
                    if (value is > 0 and < 1023 && Neighbor(at - 32, value - 1) && Neighbor(at + 32, value + 1)) values.Add((uint)value);
                }
            }
        }
        return values.Count == 1 ? values.Single() : throw new InvalidDataException("Named modifier value is missing or ambiguous: " + name);
    }
    public uint Vtable(string type) {
        var name = Encoding.ASCII.GetBytes(type + "\0"); var descriptors = new HashSet<uint>();
        foreach (var s in Sections.Where(s => (s.Flags & 0x20000000) == 0)) {
            var bytes = Bytes.AsSpan(s.Offset, s.Size); int offset = 0;
            while (offset < bytes.Length) { int found = bytes[offset..].IndexOf(name); if (found < 0) break; found += offset; offset = found + 1; if (found >= 16) descriptors.Add(s.Rva + (uint)found - 16); }
        }
        var locators = new HashSet<uint>();
        foreach (var s in Sections.Where(s => (s.Flags & 0x20000000) == 0)) for (int p = 0; p + 24 <= s.Size; p += 4) {
            int at = s.Offset + p;
            if (U32(at) == 1 && U32(at + 4) == 0 && U32(at + 20) == s.Rva + (uint)p && descriptors.Contains(U32(at + 12))) locators.Add(s.Rva + (uint)p);
        }
        var tables = new HashSet<uint>();
        foreach (var col in locators) {
            var pointer = new byte[8]; BinaryPrimitives.WriteUInt64LittleEndian(pointer, Base + col);
            foreach (var s in Sections.Where(s => (s.Flags & 0x20000000) == 0)) {
                var bytes = Bytes.AsSpan(s.Offset, s.Size); int offset = 0;
                while (offset < bytes.Length) {
                    int found = bytes[offset..].IndexOf(pointer); if (found < 0) break; found += offset; offset = found + 1;
                    if ((found & 7) != 0 || found + 16 > bytes.Length) continue;
                    ulong function = BinaryPrimitives.ReadUInt64LittleEndian(bytes[(found + 8)..]);
                    if (function >= Base && function - Base <= uint.MaxValue && Executable((uint)(function - Base))) tables.Add(s.Rva + (uint)found + 8);
                }
            }
        }
        return tables.Count == 1 ? tables.Single() : throw new InvalidDataException($"RTTI {type}: expected one primary vtable, found {tables.Count}");
    }
}
