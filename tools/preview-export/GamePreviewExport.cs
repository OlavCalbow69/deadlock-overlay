using System.Text.RegularExpressions;
using SteamDatabase.ValvePak;
using ValveResourceFormat.IO;
using ValveResourceFormat.ResourceTypes;

public static class GamePreviewExport {
    public const int Version=2;
    public static void Export(string vpk,string output,CancellationToken cancellation=default) {
        using var package=new Package();package.Read(vpk);using var loader=new GameFileLoader(package,vpk);
        var heroes=loader.LoadFileCompiled("scripts/heroes.vdata")??throw new InvalidDataException("Hero definitions unavailable");
        var data=heroes.DataBlock?.ToString()??"";var hero=Regex.Match(data,@"^\thero_hornet =\s*\r?\n\t\{(?<body>.*?)(?=^\t[\w.-]+ =|\z)",RegexOptions.Multiline|RegexOptions.Singleline);
        var modelName=Regex.Match(hero.Groups["body"].Value,"^\\t\\tm_strModelName = resource_name:\"([^\"]+)\"",RegexOptions.Multiline).Groups[1].Value;
        if(!modelName.StartsWith("models/",StringComparison.Ordinal)||!modelName.EndsWith(".vmdl",StringComparison.Ordinal))throw new InvalidDataException("Vindicta model reference unavailable");
        var resource=loader.LoadFileCompiled(modelName)??throw new InvalidDataException("Hero model unavailable");var model=(Model)resource.DataBlock!;
        Directory.CreateDirectory(output);var glb=Path.Combine(output,"source.glb");
        var exporter=new GltfModelExporter(loader){ExportMaterials=true,ExportAnimations=false,ExportExtras=false,ProgressReporter=new Progress<string>()};
        exporter.Export(resource,glb,cancellation);cancellation.ThrowIfCancellationRequested();
        PreviewPackage.Write(model,glb,Path.Combine(output,"vindicta.vpreview"));
        // The runtime needs only the bounded mesh/rig package, not glTF intermediates.
        foreach(var file in Directory.GetFiles(output,"*.glb"))File.Delete(file);
    }
}
