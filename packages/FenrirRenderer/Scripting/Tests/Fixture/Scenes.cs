using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Renderer` names the class and not the namespace.
namespace RendererScriptFixture;

// A Transform at a position, unrotated and unscaled: a C# Transform is plain data, zero when made with new.
public static class Place
{
    public static Transform At(Vector3 position) =>
        new() { Position = position, Rotation = Quaternion.Identity, Scale = Vector3.One };
}

// What a game's system does to draw something, once armed: on its next update it loads a mesh by file#Label, makes a
// builtin:unlit material and sets its colour, and spawns a MeshRenderer at (1, 2, 3) and a Camera at (0, 0, 5) looking
// along -Z. The tests read back the entities it made.
[System(Phase.Update)]
public sealed class SceneBuilder : ISystem
{
    private static bool armed;
    private static uint drawn = uint.MaxValue;
    private static uint camera = uint.MaxValue;

    public static void Arm() => armed = true;

    public static uint DrawnEntity() => drawn;

    public static uint CameraEntity() => camera;

    public void OnUpdate(in SystemContext context)
    {
        if (!armed)
            return;
        armed = false;

        AssetHandle<Mesh> mesh = Assets.Load<Mesh>("two_parts.gltf#Mesh0");
        AssetHandle<Material> material = Materials.Create(Assets.Load<Shader>("builtin:unlit"));
        Materials.SetColour(material, "baseColour", new Colour(0.25f, 0.5f, 0.75f, 1.0f));

        Entity entity = context.CreateEntity();
        entity.Add(Place.At(new Vector3(1, 2, 3)));
        entity.Add(new MeshRenderer { Mesh = mesh, Material = material });
        drawn = entity.Value;

        Entity eye = context.CreateEntity();
        eye.Add(Place.At(new Vector3(0, 0, 5)));
        eye.Add(new Camera());
        camera = eye.Value;
    }
}

// SpawnModel from a system, once armed: it spawns two_parts.gltf under the parent it was given (or none), and in the same
// call moves the root it got to (4, 5, 6), so the root must be usable at once.
[System(Phase.Update)]
public sealed class ModelSpawner : ISystem
{
    private static bool armed;
    private static uint parent = uint.MaxValue;
    private static uint root = uint.MaxValue;
    private static bool rootAliveAtOnce;

    // `under` is the parent's value, or uint.MaxValue for none.
    public static void Arm(uint under)
    {
        armed = true;
        parent = under;
        root = uint.MaxValue;
        rootAliveAtOnce = false;
    }

    public static uint Root() => root;

    public static bool RootAliveAtOnce() => rootAliveAtOnce;

    public void OnUpdate(in SystemContext context)
    {
        if (!armed)
            return;
        armed = false;

        AssetHandle<Model> model = Assets.Load<Model>("two_parts.gltf");
        Entity spawned = parent == uint.MaxValue
            ? Renderer.SpawnModel(model)
            : Renderer.SpawnModel(model, new Entity(parent));
        rootAliveAtOnce = spawned.IsValid;
        spawned.Add(Place.At(new Vector3(4, 5, 6)));
        root = spawned.Value;
    }
}
