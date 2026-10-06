if (args.Length != 2) { Console.Error.WriteLine("Usage: MapExport <map.vpk> <output-directory>"); return 1; }
try { MapExporter.Export(args[0], args[1]); return 0; }
catch (Exception e) { Console.Error.WriteLine(e); return 1; }
