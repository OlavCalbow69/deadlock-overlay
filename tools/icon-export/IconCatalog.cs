using System.Text.Json;
using System.Text.RegularExpressions;
using SteamDatabase.ValvePak;
using ValveResourceFormat;

public static class IconCatalog {
    static readonly JsonSerializerOptions Json=new(){WriteIndented=true};
    public record Coverage(int heroes,int npcs,int missing_hero_images,int missing_npc_images,int heroes_without_images,int npcs_without_icons,int non_disabled_heroes_missing_images);
    public record Image(string role,string reference,string source,string? file,string? friendly_file,int width,int height,string? error);
    public record Hero(string token,int id,string name,bool disabled,bool development,List<Image> images);
    public record Npc(string token,string type,bool generic_fallback,Image? image);

    public static Coverage Build(Package package,string vpk,string output,List<IconResult> images) {
        var bySource=images.ToDictionary(i=>i.source,StringComparer.OrdinalIgnoreCase);
        var labels=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        var game=Path.GetDirectoryName(Path.GetFullPath(vpk))!;
        foreach(var file in new[]{"citadel_heroes/citadel_heroes_english.txt","citadel_gc_hero_names/citadel_gc_hero_names_english.txt"}) {
            var path=Path.Combine(game,"resource","localization",file.Replace('/',Path.DirectorySeparatorChar));
            if(!File.Exists(path))continue;
            foreach(Match match in Regex.Matches(File.ReadAllText(path),"\"(hero_[^\"]+)\"\\s+\"([^\"]*)\"")) {
                var key=match.Groups[1].Value;
                // :n is the nominative localization form, not part of a hero token.
                if(key.EndsWith(":n",StringComparison.Ordinal))key=key[..^2];
                else if(key.Contains(':'))continue;
                labels[key]=match.Groups[2].Value;
            }
        }
        var heroes=new List<Hero>();var npcs=new List<Npc>();
        var roles=new[]{("small","m_strIconImageSmall"),("minimap","m_strMinimapImage"),("card","m_strIconHeroCard"),("critical","m_strIconHeroCardCritical"),("gloat","m_strIconHeroCardGloat")};
        foreach(var (token,data) in Definitions(package,"scripts/heroes.vdata_c")) {
            if(!token.StartsWith("hero_",StringComparison.Ordinal)||token=="hero_base")continue;
            int.TryParse(Scalar(data,"m_HeroID"),out int id);
            var name=labels.GetValueOrDefault(token,token);
            var folder="by-hero/"+Safe(id+"-"+name);var refs=new List<Image>();
            foreach(var (role,field) in roles){var reference=Text(data,field);if(reference.Length>0)refs.Add(Resolve(role,reference,folder));}
            heroes.Add(new Hero(token,id,name,Scalar(data,"m_bDisabled")=="true",Scalar(data,"m_bInDevelopment")=="true",refs));
        }
        foreach(var (token,data) in Definitions(package,"scripts/npc_units.vdata_c")) {
            var type=Text(data,"_class");if(!type.StartsWith("npc_",StringComparison.Ordinal))continue;
            var reference=Text(data,"m_strCustomUnitIcon");bool fallback=false;
            if(reference.Length==0&&type=="npc_trooper"){reference="file://{images}/npcs/trooper.psd";fallback=true;}
            else if(reference.Length==0&&type=="npc_trooper_neutral"){reference="file://{images}/npcs/neutral_bot.psd";fallback=true;}
            var image=reference.Length>0?Resolve("icon",reference,"by-npc/"+Safe(token)):null;
            npcs.Add(new Npc(token,type,fallback,image));
        }
        File.WriteAllText(Path.Combine(output,"catalog.json"),JsonSerializer.Serialize(new {heroes,npcs},Json));
        var heroMissing=heroes.SelectMany(h=>h.images).Count(i=>i.error!=null);
        var npcMissing=npcs.Count(n=>n.image?.error!=null);
        var summary=new Coverage(heroes.Count,npcs.Count,heroMissing,npcMissing,heroes.Count(h=>h.images.Count==0),npcs.Count(n=>n.image==null),heroes.Count(h=>!h.disabled&&(h.images.Count==0||h.images.Any(i=>i.error!=null))));
        File.WriteAllText(Path.Combine(output,"coverage.json"),JsonSerializer.Serialize(new {summary,heroes_without_images=heroes.Where(h=>h.images.Count==0).Select(h=>h.token),npcs_without_configured_or_generic_icon=npcs.Where(n=>n.image==null).Select(n=>n.token)},Json));
        var index=new System.Text.StringBuilder("# Game icon catalog\n\nGenerated from the installed VPK and hero/NPC definitions. Some entries are disabled/development content. NPC generic fallbacks are labeled in catalog.json.\n\n| Hero | Token | ID | Small | Minimap | Card |\n| --- | --- | --- | --- | --- | --- |\n");
        foreach(var hero in heroes.OrderBy(h=>h.name)) {
            string Link(string role){var i=hero.images.Find(i=>i.role==role);return i?.friendly_file!=null?$"[PNG]({i.friendly_file.Replace(" ","%20")})":"Unavailable";}
            index.AppendLine($"| {hero.name} | {hero.token} | {hero.id} | {Link("small")} | {Link("minimap")} | {Link("card")} |");
        }
        File.WriteAllText(Path.Combine(output,"INDEX.md"),index.ToString());
        return summary;

        Image Resolve(string role,string reference,string folder) {
            var path=reference.Replace("file://{images}/","panorama/images/",StringComparison.OrdinalIgnoreCase);
            var extension=Path.GetExtension(path).TrimStart('.');
            var source=path.EndsWith(".vtex_c",StringComparison.OrdinalIgnoreCase)?path:path.EndsWith(".vtex",StringComparison.OrdinalIgnoreCase)?path+"_c":path[..^Math.Min(extension.Length+1,path.Length)]+"_"+extension+".vtex_c";
            bySource.TryGetValue(source,out var icon);
            var error=icon==null?"Referenced texture not present in exported archive entries":icon.error;
            string? friendly=null;
            if(error==null&&icon?.file!=null) {
                friendly=folder+"/"+role+".png";var target=Path.Combine(output,friendly.Replace('/',Path.DirectorySeparatorChar));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);File.Copy(Path.Combine(output,icon.file.Replace('/',Path.DirectorySeparatorChar)),target,true);
            }
            return new Image(role,reference,source,icon?.file,friendly,icon?.width??0,icon?.height??0,error);
        }
    }
    internal static IEnumerable<(string token,string data)> Definitions(Package package,string path) {
        var entry=package.FindEntry(path)??throw new InvalidDataException("Missing definition: "+path);
        package.ReadEntry(entry,out byte[] bytes);using var resource=new Resource{FileName=path};resource.Read(new MemoryStream(bytes));
        var text=resource.DataBlock?.ToString()??throw new InvalidDataException("Missing definition DATA: "+path);
        // The serializer expands inherited KV3 values. Limit extraction to direct
        // root definitions and their immediate fields, excluding nested overrides.
        var matches=Regex.Matches(text,@"^\t(?<key>[\w.-]+) =\s*\r?\n\t\{",RegexOptions.Multiline);
        for(int i=0;i<matches.Count;++i){var start=matches[i].Index;var end=i+1<matches.Count?matches[i+1].Index:text.Length;yield return (matches[i].Groups["key"].Value,text[start..end]);}
    }
    static string Text(string data,string key){var m=Regex.Match(data,"^\\t\\t"+Regex.Escape(key)+" = (?:[a-z_]+:)?\"([^\"]*)\"",RegexOptions.Multiline);return m.Success?m.Groups[1].Value:"";}
    static string Scalar(string data,string key){var m=Regex.Match(data,"^\\t\\t"+Regex.Escape(key)+" = ([^\\r\\n]+)",RegexOptions.Multiline);return m.Success?m.Groups[1].Value.Trim():"";}
    static string Safe(string name){var bad=Path.GetInvalidFileNameChars();return string.Concat(name.Select(c=>bad.Contains(c)?'_':c)).Trim().TrimEnd('.');}
}
