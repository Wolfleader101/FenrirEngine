using Fenrir.Assets;
using Fenrir.DebugDraw;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Assets` and `DebugDraw` name the classes.
namespace PhysicsDemo.Scripts;

/// <summary>Which of the shared meshes an entity is drawn with, as the legacy DemoMesh's Kind named them.</summary>
public enum MeshKind
{
    Cube,
    Sphere,
}

/// <summary>
/// How a C# scene's bodies look, as the legacy DemoMesh drew them: the shared unit cube and sphere (one unit across, so an
/// entity's Transform scale is its size, matching a collider's default shape) in one flat colour each, lit by the scene's
/// sun under the skybox, as examples/PhysicsDemo/src/SceneLooks.hpp does for the C++ scenes. What it loads and makes is the
/// active scene's.
/// </summary>
public static class Looks
{
    // The materials made so far, one per colour; a handle the scene no longer holds (another scene's) is made again.
    private static readonly List<(Vector3 Colour, AssetHandle<Material> Material)> Made = [];

    /// <summary>Gives the active scene a sun, an ambient light and the skybox as its Sky; logs an error if anything did not load.</summary>
    public static void Begin(in SystemContext context, string scene)
    {
        Made.Clear();
        AssetHandle<Image> skybox = Assets.Load<Image>("skybox/skybox.cubemap");
        if (!Assets.IsValid(Cube()) || !Assets.IsValid(Sphere()) || !Assets.IsValid(Lit()) || !Assets.IsValid(skybox))
            context.Log.Error($"{scene}: the cube, sphere, lit shader or skybox did not load from the asset root");

        // The legacy demo's sun shone down along (-0.2, -1, -0.3); this one shines along its -Z.
        Entity sun = Entity.Create();
        var name = new Name();
        name.SetText("Sun");
        sun.Add(name);
        sun.Add(new Transform
        {
            Position = Vector3.Zero,
            Rotation = Multiply(AngleAxis(0.6f, new Vector3(0, 1, 0)), AngleAxis(-1.1f, new Vector3(1, 0, 0))),
            Scale = Vector3.One,
        });
        sun.Add(new DirectionalLight());
        Resources.Insert(new AmbientLight { Colour = new Colour(0.2f, 0.2f, 0.2f) });
        Resources.Insert((Sky)new CubemapSky { Cubemap = skybox });
    }

    /// <summary>Draws <paramref name="entity"/> as <paramref name="kind"/> in <paramref name="colour"/>.</summary>
    public static void Add(Entity entity, MeshKind kind, Vector3 colour)
    {
        if (kind == MeshKind.Sphere)
            AddSphere(entity, colour);
        else
            AddCube(entity, colour);
    }

    /// <summary>Draws <paramref name="entity"/> as the unit cube in <paramref name="colour"/>, red, green and blue in [0, 1] as the legacy scenes gave them (sRGB).</summary>
    public static void AddCube(Entity entity, Vector3 colour) =>
        entity.Add(new MeshRenderer { Mesh = Cube(), Material = Material(colour) });

    /// <summary>Draws <paramref name="entity"/> as the unit sphere (radius 0.5) in <paramref name="colour"/>.</summary>
    public static void AddSphere(Entity entity, Vector3 colour) =>
        entity.Add(new MeshRenderer { Mesh = Sphere(), Material = Material(colour) });

    /// <summary>The scene's material in <paramref name="colour"/>, made the first time a colour is asked for and shared by every entity drawn in it.</summary>
    public static AssetHandle<Material> Material(Vector3 colour)
    {
        foreach (var (made, material) in Made)
        {
            if (made.Equals(colour) && Assets.IsValid(material))
                return material;
        }
        AssetHandle<Material> created = Materials.Create(Lit());
        Materials.SetColour(created, "baseColour",
            new Colour(SrgbToLinear(colour.X), SrgbToLinear(colour.Y), SrgbToLinear(colour.Z)));
        Made.Add((colour, created));
        return created;
    }

    /// <summary>
    /// Shows each entity's Name as debug screen text, one line each from the window's top-left: the legacy scenes write their
    /// live readings into names, which the legacy editor's hierarchy showed.
    /// </summary>
    public static void ShowReadouts(IEnumerable<Entity> entities)
    {
        float y = 16;
        foreach (Entity entity in entities)
        {
            if (!entity.IsValid)
                continue;
            DebugDraw.ScreenText(new Vector2(16, y), entity.Get<Name>().Value.GetText(), Colour.White);
            y += 20;
        }
    }

    /// <summary>A Camera entity's rotation looking along the legacy DemoCamera's yaw and pitch: degrees about world +Y where -90 looks along -Z, and degrees above the horizon.</summary>
    public static Quaternion LookRotation(float yawDegrees, float pitchDegrees) =>
        Multiply(AngleAxis(-(yawDegrees + 90) * MathF.PI / 180, new Vector3(0, 1, 0)),
            AngleAxis(pitchDegrees * MathF.PI / 180, new Vector3(1, 0, 0)));

    public static Quaternion AngleAxis(float angle, Vector3 unitAxis)
    {
        float sine = MathF.Sin(angle / 2);
        return new Quaternion(unitAxis.X * sine, unitAxis.Y * sine, unitAxis.Z * sine, MathF.Cos(angle / 2));
    }

    public static Quaternion Multiply(Quaternion a, Quaternion b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);

    // Loaded again on each use: a path already loaded gives the same handle, and a hot reload keeps no fields.
    private static AssetHandle<Mesh> Cube() => Assets.Load<Mesh>("cube.obj#Mesh0");
    private static AssetHandle<Mesh> Sphere() => Assets.Load<Mesh>("sphere.obj#Mesh0");
    private static AssetHandle<Shader> Lit() => Assets.Load<Shader>("builtin:lit");

    // The sRGB transfer function's inverse (IEC 61966-2-1): the legacy colours were shown unconverted, as sRGB.
    private static float SrgbToLinear(float encoded) =>
        encoded <= 0.04045f ? encoded / 12.92f : MathF.Pow((encoded + 0.055f) / 1.055f, 2.4f);
}
