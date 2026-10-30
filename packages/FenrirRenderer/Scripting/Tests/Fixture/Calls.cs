using Fenrir.Assets;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

namespace RendererScriptFixture;

// The renderer's functions as C# calls them, from the tests outside any system. A result that is a value is kept, and
// read back a float at a time.
public static class RendererCalls
{
    private static float[] last = [];

    // The kept result's float `index`.
    public static float Value(int index) => last[index];

    // ScreenPointToRay; keeps the origin and the direction.
    public static bool Ray(uint camera, float x, float y)
    {
        bool found = Renderer.ScreenPointToRay(new Entity(camera), new Vector2(x, y), out Ray ray);
        last = [ray.Origin.X, ray.Origin.Y, ray.Origin.Z, ray.Direction.X, ray.Direction.Y, ray.Direction.Z];
        return found;
    }

    // WorldToScreenPoint; keeps the point.
    public static bool ScreenPoint(uint camera, float x, float y, float z)
    {
        bool found = Renderer.WorldToScreenPoint(new Entity(camera), new Vector3(x, y, z), out Vector2 point);
        last = [point.X, point.Y];
        return found;
    }

    public static uint CameraAt(uint window, float x, float y) =>
        Renderer.CameraAt(new WindowId(window), new Vector2(x, y)).Value;

    // GetWorldBounds; keeps the minimum and the maximum.
    public static bool Bounds(uint entity)
    {
        bool found = Renderer.GetWorldBounds(new Entity(entity), out Bounds bounds);
        last = [bounds.Min.X, bounds.Min.Y, bounds.Min.Z, bounds.Max.X, bounds.Max.Y, bounds.Max.Z];
        return found;
    }

    // Stats; keeps the draws, passes and uploads.
    public static bool Stats()
    {
        bool installed = Renderer.Stats(out RendererStats stats);
        last = [stats.Draws, stats.Passes, stats.Uploads];
        return installed;
    }

    public static uint SpawnEmptyModel() => Renderer.SpawnModel(AssetHandle<Model>.None).Value;

    public static uint SpawnUnder(uint parent) =>
        Renderer.SpawnModel(Assets.Load<Model>("two_parts.gltf"), new Entity(parent)).Value;

    public static uint None() => Entity.None.Value;

    // Spawns the model a test loaded, by its handle: a skinned one gets its SkinnedMeshes and skeletons.
    public static uint SpawnHandle(uint index, uint generation) =>
        Renderer.SpawnModel(new AssetHandle<Model>(index, generation)).Value;
}

// The scene resources as C# writes them: in place when the active scene has one, else by inserting one.
public static class ResourceCalls
{
    public static void SkyColour(float r, float g, float b)
    {
        Sky sky = new ColourSky { Colour = new Colour(r, g, b, 1.0f) };
        if (Resources.TryGet(out Ref<Sky> held))
            held.Value = sky;
        else
            Resources.Insert(sky);
    }

    public static void SkyCubemap(uint index, uint generation)
    {
        Sky sky = new CubemapSky { Cubemap = new AssetHandle<Image>(index, generation) };
        if (Resources.TryGet(out Ref<Sky> held))
            held.Value = sky;
        else
            Resources.Insert(sky);
    }

    public static void Ambient(float r, float g, float b, float intensity)
    {
        var ambient = new AmbientLight { Colour = new Colour(r, g, b, 1.0f), Intensity = intensity };
        if (Resources.TryGet(out Ref<AmbientLight> held))
            held.Value = ambient;
        else
            Resources.Insert(ambient);
    }

    // Reads the sky as C++ wrote it: 0 for a colour sky (keeping its colour), 1 for a cubemap (keeping its handle).
    public static int ReadSky()
    {
        ref Sky sky = ref Resources.Get<Sky>().Value;
        if (sky.Type == SkyType.Colour)
        {
            last = [sky.Colour.Colour.R, sky.Colour.Colour.G, sky.Colour.Colour.B];
            return 0;
        }
        last = [sky.Cubemap.Cubemap.Index, sky.Cubemap.Cubemap.Generation];
        return 1;
    }

    private static float[] last = [];

    public static float Value(int index) => last[index];
}

// What `new` makes, added from C#: the tests compare it with C++'s `{}`.
public static class NewCalls
{
    // `which`: 0 a MeshRenderer, 1 a Camera, 2 a Camera with a new Orthographic, 3 with a new ImageTarget, 4 with a new
    // ColourBackground, 5 a DirectionalLight.
    public static void Add(uint entity, int which)
    {
        var target = new Entity(entity);
        switch (which)
        {
            case 0:
                target.Add(new MeshRenderer());
                break;
            case 1:
                target.Add(new Camera());
                break;
            case 2:
                target.Add(new Camera { Projection = new Orthographic() });
                break;
            case 3:
                target.Add(new Camera { Target = new ImageTarget() });
                break;
            case 4:
                target.Add(new Camera { Background = new ColourBackground() });
                break;
            default:
                target.Add(new DirectionalLight());
                break;
        }
    }

    // A new AmbientLight, and a new Sky or (with `cubemap` not 0) a new CubemapSky, inserted into the active scene.
    public static void InsertResources(int cubemap)
    {
        Resources.Insert(new AmbientLight());
        Resources.Insert(cubemap != 0 ? (Sky)new CubemapSky() : new Sky());
    }
}
