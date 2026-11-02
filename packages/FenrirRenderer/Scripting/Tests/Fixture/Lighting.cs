using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

namespace RendererScriptFixture;

// The lighting spec's types as C# writes and reads them (Lighting.cpp): C++ builds a value with its fields off their
// defaults, C# adds it to an entity or puts it in the active scene, and reads one back by value.
public static class RoundTripCalls
{
    public static void AddCamera(uint entity, Camera value) => new Entity(entity).Add(value);
    public static Camera GetCamera(uint entity) => new Entity(entity).Get<Camera>().Value;

    public static void AddDirectionalLight(uint entity, DirectionalLight value) => new Entity(entity).Add(value);
    public static DirectionalLight GetDirectionalLight(uint entity) => new Entity(entity).Get<DirectionalLight>().Value;

    public static void AddPointLight(uint entity, PointLight value) => new Entity(entity).Add(value);
    public static PointLight GetPointLight(uint entity) => new Entity(entity).Get<PointLight>().Value;

    public static void AddSpotLight(uint entity, SpotLight value) => new Entity(entity).Add(value);
    public static SpotLight GetSpotLight(uint entity) => new Entity(entity).Get<SpotLight>().Value;

    public static void AddRectLight(uint entity, RectLight value) => new Entity(entity).Add(value);
    public static RectLight GetRectLight(uint entity) => new Entity(entity).Get<RectLight>().Value;

    public static void AddMeshRenderer(uint entity, MeshRenderer value) => new Entity(entity).Add(value);
    public static MeshRenderer GetMeshRenderer(uint entity) => new Entity(entity).Get<MeshRenderer>().Value;

    public static void AddInstancedMeshRenderer(uint entity, InstancedMeshRenderer value) => new Entity(entity).Add(value);
    public static InstancedMeshRenderer GetInstancedMeshRenderer(uint entity) =>
        new Entity(entity).Get<InstancedMeshRenderer>().Value;

    // The scene resources: written in place when the active scene has one, else inserted.
    public static void SetSky(Sky value)
    {
        if (Resources.TryGet(out Ref<Sky> held))
            held.Value = value;
        else
            Resources.Insert(value);
    }

    public static Sky GetSky() => Resources.Get<Sky>().Value;

    public static void SetSkyLight(SkyLight value)
    {
        if (Resources.TryGet(out Ref<SkyLight> held))
            held.Value = value;
        else
            Resources.Insert(value);
    }

    public static SkyLight GetSkyLight() => Resources.Get<SkyLight>().Value;

    // Renderer.Stats, whole: what the renderer did in its last frame as C# reads it.
    public static RendererStats Stats()
    {
        Renderer.Stats(out RendererStats stats);
        return stats;
    }

    public static bool SpotShadowBudget(uint budget) => Renderer.SetSpotShadowBudget(budget);

    public static bool PointShadowBudget(uint budget) => Renderer.SetPointShadowBudget(budget);
}

// An instance list made in C# and drawn by an InstancedMeshRenderer C# adds: copy i at (i, y, 0), coloured.
public static class InstanceListCalls
{
    private static AssetHandle<InstanceList> list;

    private static Matrix4x4[] CopiesAt(int count, float y)
    {
        var transforms = new Matrix4x4[count];
        for (int i = 0; i < count; ++i)
        {
            transforms[i] = Matrix4x4.Identity;
            transforms[i].Column3 = new Vector4(i, y, 0, 1);
        }
        return transforms;
    }

    // Makes a list of `count` coloured copies at height 0 and draws it from `entity` at the origin, with two_parts.gltf's
    // first mesh in builtin:unlit, seen by a camera at (2, 0, 10) on `camera`.
    public static void Draw(uint entity, uint camera, int count)
    {
        var colours = new Colour[count];
        for (int i = 0; i < count; ++i)
            colours[i] = new Colour(1, (float)i / count, 0, 1);
        list = Assets.AddInstanceList(CopiesAt(count, 0), colours);

        var drawn = new Entity(entity);
        drawn.Add(Place.At(Vector3.Zero));
        drawn.Add(new InstancedMeshRenderer
        {
            Mesh = Assets.Load<Mesh>("two_parts.gltf#Mesh0"),
            Material = Materials.Create(Assets.Load<Shader>("builtin:unlit")),
            Instances = list,
        });
        var eye = new Entity(camera);
        eye.Add(Place.At(new Vector3(2, 0, 10)));
        eye.Add(new Camera());
    }

    // Swaps the drawn list's copies for `count` white ones at height 1.
    public static bool Replace(int count) => Assets.ReplaceInstanceList(list, CopiesAt(count, 1));

    public static uint Index() => list.Index;

    public static uint Generation() => list.Generation;
}
