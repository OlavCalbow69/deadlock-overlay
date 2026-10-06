using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using SteamDatabase.ValvePak;
using ValveResourceFormat;
using ValveResourceFormat.ResourceTypes;
using ValveResourceFormat.Serialization.KeyValues;
using ValveResourceFormat.ResourceTypes.RubikonPhysics;
using Shapes=ValveResourceFormat.ResourceTypes.RubikonPhysics.Shapes;

public static class MapExporter {
public const int FormatVersion = 2;
public static object Export(string archive, string output, CancellationToken cancellation = default) {
    string[] args = [archive, output];
    using var package=new Package();package.Read(args[0]);
    var entry=(package.Entries??throw new InvalidDataException("Empty VPK")).SelectMany(k=>k.Value).FirstOrDefault(e=>e.FileName=="world_physics"&&(e.TypeName=="vphys_c"||e.TypeName=="vmdl_c"))??throw new InvalidDataException("World physics entry missing");
    Console.WriteLine($"Physics: {entry.DirectoryName}/{entry.FileName}.{entry.TypeName}");
    package.ReadEntry(entry,out byte[] bytes);
    using var resource=new Resource();resource.Read(new MemoryStream(bytes));
    var physics=resource.Blocks.OfType<PhysAggregateData>().FirstOrDefault()??resource.DataBlock as PhysAggregateData??throw new InvalidDataException("No physics aggregate");
    Directory.CreateDirectory(args[1]);var name=Path.GetFileNameWithoutExtension(args[0]);var destination=Path.Combine(args[1],name+".tri");var temporary=destination+".tmp";
    long triangles=0;int hulls=0,meshes=0,skipped=0,compounds=0;Vector3 min=new(float.MaxValue),max=new(float.MinValue);
    var tags=physics.CollisionAttributes.Select(a=>a.GetArray<string>("m_InteractAsStrings")??a.GetArray<string>("m_PhysicsTagStrings")??Array.Empty<string>()).ToArray();
    using(var stream=new FileStream(temporary,FileMode.Create,FileAccess.Write,FileShare.None,1024*1024))
    using(var writer=new BinaryWriter(stream)){
        void Write(Vector3 a,Vector3 b,Vector3 c,Matrix4x4 pose){
            a=Vector3.Transform(a,pose);b=Vector3.Transform(b,pose);c=Vector3.Transform(c,pose);
            foreach(var v in new[]{a,b,c})if(!float.IsFinite(v.X)||!float.IsFinite(v.Y)||!float.IsFinite(v.Z))throw new InvalidDataException("Nonfinite vertex");
            if(Vector3.Cross(b-a,c-a).LengthSquared()<1e-10f)return;
            foreach(var v in new[]{a,b,c}){writer.Write(v.X);writer.Write(v.Y);writer.Write(v.Z);min=Vector3.Min(min,v);max=Vector3.Max(max,v);}triangles++;
        }
        bool Include(int index){
            if(index<0||index>=tags.Length)throw new InvalidDataException("Invalid collision attribute");
            // Trigger volumes, player clip and tools-only shapes must not become visual walls.
            return tags[index].Length==0||tags[index].Any(t=>t=="solid"||t=="CONTENTS_SOLID"||t=="world"||t=="default"||t=="opaque"||t=="blocklos"||t=="Citadel_Foliage");
        }
        void Capsule(Vector3 a,Vector3 b,float radius,Matrix4x4 pose){
            if(!float.IsFinite(radius)||radius<=0)throw new InvalidDataException("Invalid capsule radius");
            var axis=b-a;axis=axis.LengthSquared()<1e-8f?Vector3.UnitZ:Vector3.Normalize(axis);
            var x=Vector3.Normalize(Vector3.Cross(axis,Math.Abs(axis.Z)<.9f?Vector3.UnitZ:Vector3.UnitX));var y=Vector3.Cross(axis,x);
            var rings=new List<Vector3[]>();
            for(int hemisphere=0;hemisphere<2;hemisphere++)for(int j=0;j<=8;j++){
                float theta=(hemisphere==0?-MathF.PI/2:0)+j*MathF.PI/16;var center=hemisphere==0?a:b;var ring=new Vector3[32];
                for(int k=0;k<32;k++){float angle=k*MathF.PI/16;ring[k]=center+radius*(axis*MathF.Sin(theta)+(x*MathF.Cos(angle)+y*MathF.Sin(angle))*MathF.Cos(theta));}rings.Add(ring);
            }
            for(int j=0;j+1<rings.Count;j++)for(int k=0;k<32;k++){int next=(k+1)%32;Write(rings[j][k],rings[j+1][k],rings[j+1][next],pose);Write(rings[j][k],rings[j+1][next],rings[j][next],pose);}
        }
        for(int partIndex=0;partIndex<physics.Parts.Length;partIndex++){
            cancellation.ThrowIfCancellationRequested();
            var part=physics.Parts[partIndex];var pose=partIndex<physics.BindPose.Length?physics.BindPose[partIndex]:Matrix4x4.Identity;
            var allHulls=part.Shape.Hulls.ToList();var allMeshes=part.Shape.Meshes.ToList();var allSpheres=part.Shape.Spheres.ToList();var allCapsules=part.Shape.Capsules.ToList();
            // NuGet 20 predates compound support. Decode every child using its parent attributes.
            var compoundArray=physics.Data.GetArray("m_parts")[partIndex].GetSubCollection("m_rnShape").GetArray("m_compounds");
            if(compoundArray!=null)foreach(var descriptor in compoundArray){
                compounds++;int attribute=descriptor.GetInt32Property("m_nCollisionAttributeIndex");var compound=descriptor.GetSubCollection("m_Compound")??throw new InvalidDataException("Missing compound data");int children=0;
                foreach(var h in compound.GetArray("m_Hulls")??[]){allHulls.Add(new HullDescriptor{CollisionAttributeIndex=attribute,Shape=new Shapes.Hull(h)});children++;}
                foreach(var m in compound.GetArray("m_Meshes")??[]){allMeshes.Add(new MeshDescriptor{CollisionAttributeIndex=attribute,Shape=new Shapes.Mesh(m)});children++;}
                foreach(var s in compound.GetArray("m_Spheres")??[]){allSpheres.Add(new SphereDescriptor{CollisionAttributeIndex=attribute,Shape=new Shapes.Sphere(s)});children++;}
                foreach(var c in compound.GetArray("m_Capsules")??[]){allCapsules.Add(new CapsuleDescriptor{CollisionAttributeIndex=attribute,Shape=new Shapes.Capsule(c)});children++;}
                if(children!=compound.GetInt32Property("m_nShapeCount"))throw new InvalidDataException("Compound child count mismatch");
            }
            foreach(var s in allSpheres){if(Include(s.CollisionAttributeIndex))Capsule(s.Shape.Center,s.Shape.Center,s.Shape.Radius,pose);else skipped++;}
            foreach(var s in allCapsules){if(Include(s.CollisionAttributeIndex))Capsule(s.Shape.Center[0],s.Shape.Center[1],s.Shape.Radius,pose);else skipped++;}
            foreach(var descriptor in allHulls){
                if(!Include(descriptor.CollisionAttributeIndex)){skipped++;continue;}
                var hull=descriptor.Shape;var vertices=hull.GetVertexPositions();var edges=hull.GetEdges();
                foreach(var face in hull.GetFaces()){
                    var polygon=new List<int>();int edge=face.Edge;
                    do {if(edge<0||edge>=edges.Length||polygon.Count>edges.Length)throw new InvalidDataException("Invalid hull edge loop");polygon.Add(edges[edge].Origin);edge=edges[edge].Next;}while(edge!=face.Edge);
                    for(int i=1;i+1<polygon.Count;i++)Write(vertices[polygon[0]],vertices[polygon[i]],vertices[polygon[i+1]],pose);
                }hulls++;
            }
            foreach(var descriptor in allMeshes){
                if(!Include(descriptor.CollisionAttributeIndex)){skipped++;continue;}
                var mesh=descriptor.Shape;var vertices=mesh.GetVertices();foreach(var t in mesh.GetTriangles())Write(vertices[(int)t.X],vertices[(int)t.Y],vertices[(int)t.Z],pose);meshes++;
            }
        }
    }
    if(triangles==0)throw new InvalidDataException("No solid collision triangles extracted");
    File.Move(temporary,destination,true);
    using var sourceStream=File.OpenRead(args[0]);
    var metadata=new {exporterVersion=FormatVersion,meshSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(destination))),map=name,source=Path.GetFullPath(args[0]),sourceSha256=Convert.ToHexString(SHA256.HashData(sourceStream)),physicsEntry=$"{entry.DirectoryName}/{entry.FileName}.{entry.TypeName}",triangles,hulls,meshes,compounds,skipped,min=new[]{min.X,min.Y,min.Z},max=new[]{max.X,max.Y,max.Z},tags};
    File.WriteAllText(destination+".json",JsonSerializer.Serialize(metadata,new JsonSerializerOptions{WriteIndented=true}));
    Console.WriteLine(JsonSerializer.Serialize(metadata));return metadata;
}
}

