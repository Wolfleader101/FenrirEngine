using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Assets` names the class and not the namespace.
namespace AssetsScriptFixture;

// Handles cross to the tests as their bits (generation high, index low), which the tests unpack.
public static class Bits
{
    public static ulong Of<T>(AssetHandle<T> handle) where T : class => ((ulong)handle.Generation << 32) | handle.Index;

    public static AssetHandle<T> To<T>(ulong bits) where T : class => new((uint)bits, (uint)(bits >> 32));
}

// What a game's system does with meshes, once armed: on its first update it loads a model by path and one of its meshes by
// file#Label, and adds a triangle; on its second it replaces the triangle with a quad. The tests read what it got.
[System(Phase.Update)]
public sealed class MeshBuilder : ISystem
{
    private static int step;

    private static ulong model;
    private static ulong labelledMesh;
    private static ulong addedMesh;
    private static bool replaced;

    public static ulong ModelHandle() => model;

    public static ulong LabelledMeshHandle() => labelledMesh;

    public static ulong AddedMeshHandle() => addedMesh;

    public static bool Replaced() => replaced;

    // Starts the two steps on the next update.
    public static void Arm()
    {
        step = 1;
        model = 0;
        labelledMesh = 0;
        addedMesh = 0;
        replaced = false;
    }

    public void OnUpdate(in SystemContext context)
    {
        if (step == 1)
        {
            model = Bits.Of(Assets.Load<Model>("models/scene.gltf"));
            labelledMesh = Bits.Of(Assets.Load<Mesh>("models/scene.gltf#Mesh0"));
            // A triangle from (-1, 0, 0) to (2, 3, 0).
            ReadOnlySpan<Vector3> positions = [new(-1, 0, 0), new(2, 0, 0), new(0, 3, 0)];
            ReadOnlySpan<Vector3> normals = [new(0, 0, 1), new(0, 0, 1), new(0, 0, 1)];
            ReadOnlySpan<Vector2> uvs = [new(0, 0), new(1, 0), new(0, 1)];
            ReadOnlySpan<uint> indices = [0, 1, 2];
            addedMesh = Bits.Of(Assets.AddMesh(positions, normals, uvs, indices));
            step = 2;
        }
        else if (step == 2)
        {
            // A quad from (-2, -1, 0) to (4, 5, 1), with no normals or UVs.
            ReadOnlySpan<Vector3> positions = [new(-2, -1, 0), new(4, -1, 0), new(4, 5, 1), new(-2, 5, 1)];
            ReadOnlySpan<uint> indices = [0, 1, 2, 0, 2, 3];
            replaced = Assets.ReplaceMesh(Bits.To<Mesh>(addedMesh), positions, [], [], indices);
            step = 3;
        }
    }
}

// The store's functions as C# calls them, from the tests outside any system.
public static class AssetCalls
{
    // `owner` is 0 for the active scene and 1 for the app, as AssetOwner.
    public static ulong LoadMesh(string path, int owner) => Bits.Of(Assets.Load<Mesh>(path, (AssetOwner)owner));

    public static ulong LoadImage(string path) => Bits.Of(Assets.Load<Image>(path));

    public static ulong LoadModel(string path) => Bits.Of(Assets.Load<Model>(path));

    public static ulong LoadShader(string path) => Bits.Of(Assets.Load<Shader>(path));

    public static ulong LoadMaterial(string path) => Bits.Of(Assets.Load<Material>(path));

    public static bool IsValidMesh(ulong mesh) => Assets.IsValid(Bits.To<Mesh>(mesh));

    public static bool IsValidModel(ulong model) => Assets.IsValid(Bits.To<Model>(model));

    public static bool UnloadMesh(ulong mesh) => Assets.Unload(Bits.To<Mesh>(mesh));

    public static ulong AddTriangle(int owner)
    {
        ReadOnlySpan<Vector3> positions = [new(-1, 0, 0), new(2, 0, 0), new(0, 3, 0)];
        ReadOnlySpan<uint> indices = [0, 1, 2];
        return Bits.Of(Assets.AddMesh(positions, [], [], indices, (AssetOwner)owner));
    }

    // A mesh whose one index points past its vertices, which the store refuses.
    public static ulong AddBrokenMesh()
    {
        ReadOnlySpan<Vector3> positions = [new(0, 0, 0), new(1, 0, 0), new(0, 1, 0)];
        ReadOnlySpan<uint> indices = [0, 1, 7];
        return Bits.Of(Assets.AddMesh(positions, [], [], indices));
    }
}
