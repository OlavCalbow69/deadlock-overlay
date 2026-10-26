using System.Reflection;
using SteamDatabase.ValvePak;
using ValveResourceFormat;
using ValveResourceFormat.IO;
using ValveResourceFormat.ResourceTypes;
using SharpGLTF.Schema2;

if(args.Length==1&&args[0]=="--api") {
    foreach(var type in new[]{typeof(ValveResourceFormat.ResourceTypes.ModelData.Hitbox),typeof(ValveResourceFormat.ResourceTypes.ModelAnimation.Bone),typeof(SharpGLTF.Schema2.MaterialChannel),typeof(SharpGLTF.Memory.MemoryImage),typeof(SharpGLTF.Schema2.Texture)}) {
        Console.WriteLine(type.FullName);foreach(var member in type.GetMembers(BindingFlags.Public|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly))Console.WriteLine("  "+member);
    }return 0;
}
if(args.Length!=3){Console.Error.WriteLine("PreviewExport <pak01_dir.vpk> <model.vmdl_c> <output.glb>");return 2;}
using var package=new Package();package.Read(args[0]);
using var loader=new GameFileLoader(package,args[0]);
var resource=loader.LoadFile(args[1])??throw new FileNotFoundException(args[1]);
var model=(Model)resource.DataBlock!;
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[2]))!);
Console.WriteLine("Model: "+args[1]);
Console.WriteLine("Skeleton: "+model.GetType().GetProperty("Skeleton")?.GetValue(model));
foreach(var bone in model.Skeleton.Bones.Where(b=>b.Name is "head" or "pelvis" or "ankle_L"))Console.WriteLine($"Bone {bone.Name}: {bone.BindPose.Translation}");
foreach(var set in model.HitboxSets)Console.WriteLine($"Hitboxes {set.Key}: {set.Value.Length}");
var exporter=new GltfModelExporter(loader){ExportMaterials=true,ExportAnimations=false,ExportExtras=true,ProgressReporter=new Progress<string>(Console.WriteLine)};
exporter.Export(resource,args[2]);Console.WriteLine("Exported "+args[2]);
PreviewPackage.Write(model,args[2],Path.ChangeExtension(args[2],".vpreview"));
return 0;
