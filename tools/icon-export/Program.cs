using System.Text.Json;
if(args.Length is <2 or >3||(args.Length==3&&args[2]!="--catalog-only")){Console.Error.WriteLine("IconExport <pak01_dir.vpk> <output-directory> [--catalog-only]");return 1;}
var result=GameAssetExport.Run(args[0],args[1],args.Length==3);Console.WriteLine(JsonSerializer.Serialize(result));return result.failed==0?0:2;
