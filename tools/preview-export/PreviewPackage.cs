using System.Numerics;
using System.Text;
using SharpGLTF.Schema2;
using SkiaSharp;
using ValveResourceFormat.ResourceTypes;

public static class PreviewPackage {
    record Vertex(Vector3 Position,Vector3 Normal,Vector2 Uv);
    record Part(uint First,uint Count,Vector4 Color,byte[] Texture);
    // VRF bakes pitch AND roll into glTF: Source (x,y,z) becomes (y,z,x).
    // Undo that exact basis for meshes; the source bind-pose rig is already Z-up.
    static Vector3 Source(Vector3 p)=>new(p.Z/.0254f,p.X/.0254f,p.Y/.0254f);
    static Vector3 SourceNormal(Vector3 p)=>new(p.Z,p.X,p.Y);
    public static void Write(Model source,string glb,string destination) {
        var model=ModelRoot.Load(glb);var vertices=new List<Vertex>();var indices=new List<uint>();var parts=new List<Part>();
        foreach(var node in model.LogicalNodes.Where(n=>n.Mesh!=null&&!n.Name.Contains("ghost_glow",StringComparison.OrdinalIgnoreCase)))foreach(var primitive in node.Mesh.Primitives) {
            if(primitive.DrawPrimitiveType!=PrimitiveType.TRIANGLES)continue;
            var positions=primitive.GetVertexAccessor("POSITION").AsVector3Array();
            var normals=primitive.GetVertexAccessor("NORMAL")?.AsVector3Array();var uv=primitive.GetVertexAccessor("TEXCOORD_0")?.AsVector2Array();
            uint firstVertex=(uint)vertices.Count,firstIndex=(uint)indices.Count;
            for(int i=0;i<positions.Count;i++)vertices.Add(new(Source(Vector3.Transform(positions[i],node.WorldMatrix)),Vector3.Normalize(SourceNormal(Vector3.TransformNormal(normals?[i]??Vector3.UnitY,node.WorldMatrix))),uv?[i]??Vector2.Zero));
            foreach(var (a,b,c) in primitive.GetTriangleIndices()){indices.Add(firstVertex+(uint)a);indices.Add(firstVertex+(uint)b);indices.Add(firstVertex+(uint)c);}
            var channel=primitive.Material?.FindChannel("BaseColor");byte[] texture=[];
            var image=channel?.Texture?.PrimaryImage;
            if(image!=null) {
                using var original=SKBitmap.Decode(image.Content.Content.ToArray());
                using var resized=new SKBitmap(Math.Min(original.Width,512),Math.Min(original.Height,512));
                using(var canvas=new SKCanvas(resized)){canvas.Clear(SKColors.Transparent);canvas.DrawBitmap(original,new SKRect(0,0,resized.Width,resized.Height),new SKSamplingOptions(SKFilterMode.Linear));}
                using var encoded=SKImage.FromBitmap(resized);using var png=encoded.Encode(SKEncodedImageFormat.Png,100);texture=png.ToArray();
            }
            parts.Add(new(firstIndex,(uint)indices.Count-firstIndex,channel?.Color??Vector4.One,texture));
        }
        var bones=source.Skeleton.Bones;var worlds=new Matrix4x4[bones.Length];var done=new bool[bones.Length];
        Matrix4x4 World(int index) {if(done[index])return worlds[index];var bone=bones[index];worlds[index]=bone.BindPose*(bone.Parent!=null?World(bone.Parent.Index):Matrix4x4.Identity);done[index]=true;return worlds[index];}
        for(int i=0;i<bones.Length;i++)World(i);
        var boxes=source.HitboxSets.GetValueOrDefault("default")??source.HitboxSets.FirstOrDefault().Value??[];
        boxes=boxes.Where(b=>source.Skeleton.GetBoneIndex(b.BoneName)>=0).ToArray();
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(destination))!);
        using var file=File.Create(destination);using var writer=new BinaryWriter(file,Encoding.UTF8);
        writer.Write(Encoding.ASCII.GetBytes("VANTAPRV"));writer.Write(1u);writer.Write((uint)vertices.Count);writer.Write((uint)indices.Count);writer.Write((uint)parts.Count);writer.Write((uint)bones.Length);writer.Write((uint)boxes.Length);
        void Text(string text){var bytes=Encoding.UTF8.GetBytes(text);writer.Write((uint)bytes.Length);writer.Write(bytes);}
        void V3(Vector3 v){writer.Write(v.X);writer.Write(v.Y);writer.Write(v.Z);}
        Text("Vindicta");Text(source.Name??"models/heroes_staging/hornet_v3/hornet.vmdl");writer.Write(3);
        foreach(var v in vertices){V3(v.Position);V3(v.Normal);writer.Write(v.Uv.X);writer.Write(v.Uv.Y);}
        foreach(var index in indices)writer.Write(index);
        foreach(var p in parts){writer.Write(p.First);writer.Write(p.Count);writer.Write(p.Color.X);writer.Write(p.Color.Y);writer.Write(p.Color.Z);writer.Write(p.Color.W);writer.Write((uint)p.Texture.Length);writer.Write(p.Texture);}
        for(int i=0;i<bones.Length;i++){var bone=bones[i];Text(bone.Name);writer.Write(bone.Parent?.Index??-1);var transform=World(i);V3(transform.Translation);var rotation=Quaternion.CreateFromRotationMatrix(transform);writer.Write(rotation.X);writer.Write(rotation.Y);writer.Write(rotation.Z);writer.Write(rotation.W);}
        foreach(var box in boxes){writer.Write(source.Skeleton.GetBoneIndex(box.BoneName));writer.Write((int)box.ShapeType);V3(box.MinBounds);V3(box.MaxBounds);writer.Write(box.ShapeRadius);writer.Write(box.GroupId);writer.Write(box.TranslationOnly?1u:0u);}
        var min=new Vector3(float.MaxValue);var max=new Vector3(float.MinValue);foreach(var v in vertices){min=Vector3.Min(min,v.Position);max=Vector3.Max(max,v.Position);}
        Console.WriteLine($"Preview: {vertices.Count} vertices, {indices.Count/3} triangles, {parts.Count} materials, {bones.Length} bones, {boxes.Length} hitboxes");
        Console.WriteLine($"Mesh source bounds {min} .. {max}");
        foreach(var bone in bones.Where(b=>b.Name is "head" or "pelvis" or "ankle_L"))Console.WriteLine($"World bone {bone.Name}: {World(bone.Index).Translation}");
        Console.WriteLine("Hitbox shapes: "+string.Join(",",boxes.Select(b=>$"{b.ShapeType}={(int)b.ShapeType}").Distinct()));
    }
}
