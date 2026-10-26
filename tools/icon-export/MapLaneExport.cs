using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using SteamDatabase.ValvePak;
using ValveResourceFormat;

// Installed map paths provide a fallback when a replicated rift marker has no
// lane number. These are map coordinates, not a hand-authored left/right guess.
public static class MapLaneExport {
    public const int Version=1;
    public static int Export(string archive,string output,CancellationToken cancel) {
        using var package=new Package();package.Read(archive);
        var entry=package.Entries?.SelectMany(k=>k.Value).FirstOrDefault(e=>e.FileName=="default_ents"&&e.TypeName=="vents_c");
        if(entry==null)return 0;
        package.ReadEntry(entry,out byte[] bytes);cancel.ThrowIfCancellationRequested();
        string hash=Convert.ToHexString(SHA256.HashData(bytes)),map=Path.GetFileNameWithoutExtension(archive);
        Directory.CreateDirectory(output);string target=Path.Combine(output,map+".tsv"),stamp=target+".source";
        string identity=$"{Version}:{hash}";
        if(File.Exists(target)&&File.Exists(stamp)&&File.ReadAllText(stamp)==identity)return 0;
        using var resource=new Resource{FileName=entry.GetFullPath()};resource.Read(new MemoryStream(bytes));
        string text=resource.DataBlock?.ToString()??"";
        var rows=new StringBuilder($"# Vanta map lanes v1 {map} {hash}\n");int paths=0;
        foreach(Match block in Regex.Matches(text,@"(?ms)^\t{4}values =\s*\r?\n\t{4}\{(?<body>.*?)^\t{4}\}")) {
            cancel.ThrowIfCancellationRequested();string body=block.Groups["body"].Value;
            if(!Regex.IsMatch(body,"classname = \"citadel_zipline_path\""))continue;
            var laneMatch=Regex.Match(body,"lane_number = \"(\\d+)\"");if(!laneMatch.Success)continue;
            int lane=int.Parse(laneMatch.Groups[1].Value,CultureInfo.InvariantCulture);if(lane is not (1 or 3 or 4 or 6))continue;
            Vector3 Vector(string key,Vector3 fallback) {
                var found=Regex.Match(body,Regex.Escape(key)+@" = \[ ([^\]]+) \]");if(!found.Success)return fallback;
                var parts=found.Groups[1].Value.Split(',').Select(x=>float.Parse(x,CultureInfo.InvariantCulture)).ToArray();
                return parts.Length==3?new(parts[0],parts[1],parts[2]):fallback;
            }
            Vector3 origin=Vector("origin",new(float.NaN)),angles=Vector("angles",Vector3.Zero),scale=Vector("scales",Vector3.One);
            if(!float.IsFinite(origin.X)||angles.LengthSquared()>.0001f)continue;
            var path=Regex.Match(body,"(?s)pathNodes = \"\"\"(?<nodes>.*?)\"\"\"");if(!path.Success)continue;
            var values=Regex.Matches(path.Groups["nodes"].Value,@"[-+]?(?:\d*\.)?\d+(?:[eE][-+]?\d+)?").Select(x=>float.Parse(x.Value,CultureInfo.InvariantCulture)).ToArray();
            if(values.Length<18||values.Length%9!=0||values.Length>9*256||values.Any(x=>!float.IsFinite(x)))continue;
            int id=paths++;
            Vector3 Node(int i,int offset)=>origin+new Vector3(values[i*9+offset],values[i*9+offset+1],values[i*9+offset+2])*scale;
            void Point(Vector3 p){if(float.IsFinite(p.X)&&float.IsFinite(p.Y)&&float.IsFinite(p.Z)&&Math.Abs(p.X)<200000&&Math.Abs(p.Y)<200000)rows.AppendLine(FormattableString.Invariant($"path\t{id}\t{lane}\t{p.X}\t{p.Y}\t{p.Z}"));}
            for(int i=0;i<values.Length/9-1;i++) {
                var a=Node(i,0);var b=Node(i+1,0);var c=a+(Node(i,6)-origin);var d=b+(Node(i+1,3)-origin);
                for(int j=0;j<16;j++){float t=j/16f,u=1-t;Point(a*(u*u*u)+c*(3*u*u*t)+d*(3*u*t*t)+b*(t*t*t));}
            }
            Point(Node(values.Length/9-1,0));
        }
        File.WriteAllText(target+".tmp",rows.ToString(),new UTF8Encoding(false));File.Move(target+".tmp",target,true);
        File.WriteAllText(stamp,identity);return paths;
    }
}
