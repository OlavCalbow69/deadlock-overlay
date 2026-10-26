using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using SteamDatabase.ValvePak;
using ValveResourceFormat.Utils;

// Runtime tables are deliberately simple UTF-8 TSV: no JSON dependency in the overlay.
// Subclass tokens use the engine's StringToken hash, rather than a model-name guess.
public static class RuntimeAssets {
    public static void Build(Package package,string output,List<IconResult> images) {
        var options=new JsonSerializerOptions{PropertyNameCaseInsensitive=true};
        using var catalog=JsonDocument.Parse(File.ReadAllText(Path.Combine(output,"catalog.json")));
        var rows=new StringBuilder("# Vanta local game assets v1\n");
        foreach(var h in catalog.RootElement.GetProperty("heroes").EnumerateArray()) {
            string Image(string role)=>h.GetProperty("images").EnumerateArray().Where(x=>x.GetProperty("role").GetString()==role&&x.GetProperty("error").ValueKind==JsonValueKind.Null).Select(x=>x.GetProperty("file").GetString()??"").FirstOrDefault()??"";
            rows.AppendLine($"hero\t{h.GetProperty("id").GetInt32()}\t{Image("small")}\t{Image("minimap")}\t{h.GetProperty("name").GetString()}");
        }
        var unitIcons=catalog.RootElement.GetProperty("npcs").EnumerateArray().ToDictionary(n=>n.GetProperty("token").GetString()!,n=>n.GetProperty("image").ValueKind==JsonValueKind.Object?n.GetProperty("image").GetProperty("file").GetString()??"":"");
        foreach(var file in new[]{"scripts/npc_units.vdata_c","scripts/misc.vdata_c"})foreach(var (token,data) in IconCatalog.Definitions(package,file)) {
            string Text(string key){var m=Regex.Match(data,"^\\t\\t"+Regex.Escape(key)+" = (?:[a-z_]+:)?\"([^\"]*)\"",RegexOptions.Multiline);return m.Success?m.Groups[1].Value:"";}
            float Number(string key){var m=Regex.Match(data,"^\\t\\t"+Regex.Escape(key)+" = ([^\\r\\n]+)",RegexOptions.Multiline);return m.Success&&float.TryParse(m.Groups[1].Value,NumberStyles.Float,CultureInfo.InvariantCulture,out var n)?n:0;}
            string type=Text("_class"),category="",icon=unitIcons.GetValueOrDefault(token,"");
            if(type=="npc_trooper")category="lane";
            else if(type.StartsWith("npc_trooper_neutral",StringComparison.Ordinal))category="jungle";
            else if(type.StartsWith("citadel_breakable_prop",StringComparison.Ordinal)) {category="breakable";icon=Text("m_eRollType").Contains("Powerup")?"minimap/item_crate_spawn_psd.png":"minimap/gold_crate_marker_psd.png";}
            else if(type=="citadel_item_powerup_spawner"||token=="citadel_item_powerup_spawner"){category="spawner";icon="minimap/powerup_souls_png.png";}
            else if(token.Contains("gold_pickup")||token=="souls_powerup_pickup"){category="souls";icon="minimap/gold_psd.png";}
            else if(token.EndsWith("powerup_pickup",StringComparison.Ordinal)||token.Contains("pickup_health")){
                category="pickup";icon=token=="gun_powerup_pickup"?"minimap/powerup_weapon.png":token=="casting_powerup_pickup"?"minimap/powerup_magic.png":token=="movement_powerup_pickup"?"minimap/powerup_movement.png":"minimap/powerup_health.png";
            }
            if(category.Length==0)continue;
            if(icon.Length==0)icon=category=="lane"?"npcs/trooper_psd.png":"npcs/neutral_bot_psd.png";
            float respawn=Number("m_flRespawnTime"),test=Number("m_flRespawnTimeTest");
            rows.AppendLine(FormattableString.Invariant($"unit\t{StringToken.Get(token.AsSpan())}\t{category}\t{icon}\t{respawn}\t{test}\t{token}"));
        }
        foreach(var image in images.Where(i=>i.error==null&&i.file!=null&&i.source.StartsWith("panorama/images/overviews/",StringComparison.Ordinal))) {
            var name=Regex.Replace(Path.GetFileNameWithoutExtension(image.file!),@"_(tga|psd)(_[0-9a-f]{8})?$","");
            // Current official HUD uses the midtown layers for the main map.
            var texture=name=="caldera"?"minimap/base/minimap_midtown_mid_psd.png":image.file;
            rows.AppendLine($"map\t{name}\t{texture}");
        }
        File.WriteAllText(Path.Combine(output,"runtime.tsv"),rows.ToString(),new UTF8Encoding(false));
    }
}
