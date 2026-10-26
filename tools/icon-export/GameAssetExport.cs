using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using SteamDatabase.ValvePak;
using ValveResourceFormat;
using ValveResourceFormat.IO;
using ValveResourceFormat.ResourceTypes;
using SkiaSharp;

public record IconResult(string source,string? file,int width,int height,string? error);
public record GameAssetExportResult(int exported,int failed,string output,IconCatalog.Coverage coverage);
public static class GameAssetExport {
    public const int Version=2;
    public static GameAssetExportResult Run(string vpk,string output,bool catalogOnly=false,CancellationToken cancel=default) {
        output=Path.GetFullPath(output);Directory.CreateDirectory(output);
        using var package=new Package();package.Read(vpk);
        var filter=new Regex(@"^panorama/images/(heroes/(?!guns/).+|npcs/.+|minimap/.+|overviews/.+)\.vtex_c$",RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
        var rows=catalogOnly?JsonSerializer.Deserialize<List<IconResult>>(File.ReadAllText(Path.Combine(output,"manifest.json")))??throw new InvalidDataException("Missing image manifest"):new List<IconResult>();
        if(!catalogOnly)foreach(var entry in package.Entries!.SelectMany(k=>k.Value)) {
            cancel.ThrowIfCancellationRequested();var path=entry.GetFullPath();bool vector=Regex.IsMatch(path,@"^panorama/images/minimap/powerup_(weapon|health|magic|movement)\.vsvg_c$");
            if(!vector&&!filter.IsMatch(path))continue;string? error=null,file=null;int width=0,height=0;
            try {
                package.ReadEntry(entry,out byte[] bytes);using var resource=new Resource{FileName=path};resource.Read(new MemoryStream(bytes));
                file=path.Replace("panorama/images/","").Replace(vector?".vsvg_c":".vtex_c",".png");var destination=Path.Combine(output,file.Replace('/',Path.DirectorySeparatorChar));Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                if(vector){var svg=resource.DataBlock?.ToString()??throw new InvalidDataException("Empty SVG");File.WriteAllBytes(destination,VectorPng(svg));width=height=128;}
                else {var texture=(Texture)resource.DataBlock!;using var bitmap=texture.GenerateBitmap();width=bitmap.Width;height=bitmap.Height;File.WriteAllBytes(destination,TextureExtract.ToPngImage(bitmap));}
            }catch(Exception ex)when(ex is not OperationCanceledException){error=ex.Message;}
            rows.Add(new(path,file,width,height,error));
        }
        File.WriteAllText(Path.Combine(output,"manifest.json"),JsonSerializer.Serialize(rows,new JsonSerializerOptions{WriteIndented=true}));
        var coverage=IconCatalog.Build(package,vpk,output,rows);RuntimeAssets.Build(package,output,rows);
        return new(rows.Count(r=>r.error==null),rows.Count(r=>r.error!=null),output,coverage);
    }
    static byte[] VectorPng(string svg) {
        var root=XElement.Parse(svg);var view=(root.Attribute("viewBox")?.Value??"0 0 33 32").Split(' ',StringSplitOptions.RemoveEmptyEntries).Select(v=>float.Parse(v,CultureInfo.InvariantCulture)).ToArray();
        if(view.Length!=4||view[2]<=0||view[3]<=0)throw new InvalidDataException("Invalid vector view box");
        // The four installed powerup assets are flat filled paths; reject richer
        // SVGs instead of silently dropping transforms, strokes or clipping.
        if(root.Descendants().Any(e=>e.Name.LocalName!="path"||e.Attribute("transform")!=null||e.Attribute("stroke")!=null))throw new InvalidDataException("Unsupported powerup vector structure");
        using var bitmap=new SKBitmap(128,128,SKColorType.Rgba8888,SKAlphaType.Premul);using var canvas=new SKCanvas(bitmap);canvas.Clear(SKColors.Transparent);
        float scale=128/Math.Max(view[2],view[3]);canvas.Translate((128-view[2]*scale)/2,(128-view[3]*scale)/2);canvas.Scale(scale);canvas.Translate(-view[0],-view[1]);
        foreach(var element in root.Elements()) {
            using var path=SKPath.ParseSvgPathData(element.Attribute("d")?.Value??"")??throw new InvalidDataException("Invalid vector path");
            var fill=element.Attribute("fill")?.Value??"white";if(fill=="none")continue;
            float opacity=float.TryParse(element.Attribute("fill-opacity")?.Value,NumberStyles.Float,CultureInfo.InvariantCulture,out var alpha)?alpha:1;
            using var paint=new SKPaint{IsAntialias=true,Style=SKPaintStyle.Fill,Color=(fill=="white"?SKColors.White:SKColor.Parse(fill)).WithAlpha((byte)Math.Clamp(opacity*255,0,255))};canvas.DrawPath(path,paint);
        }
        using var image=SKImage.FromBitmap(bitmap);using var png=image.Encode(SKEncodedImageFormat.Png,100);return png.ToArray();
    }
}
