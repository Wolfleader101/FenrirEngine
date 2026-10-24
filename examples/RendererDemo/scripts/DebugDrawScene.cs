using Fenrir.Assets;
using Fenrir.DebugDraw;
using Fenrir.Demo;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `DebugDraw` and `Assets` name the classes.
namespace RendererDemo.Scripts;

/// <summary>
/// "Debug draw (C#)": scene 4, "Debug draw", built and drawn entirely from C# through the Fenrir.DebugDraw facade
/// (examples/RendererDemo/src/DebugDrawScene.cpp is its C++ twin, and the two must look the same). Every wire and solid
/// shape in a depth-tested block behind a wall and an on-top block through it, with world-text labels; shapes relative to
/// an orbiting sphere; screen text and keyed messages; 1, 2 and 3 toggle groups, T draws for three seconds, F draws
/// forever and X clears.
/// </summary>
[System(Phase.Update)]
public sealed class DebugDrawScene : ISystem
{
    /// <summary>The scene's number in RendererDemo's C++ scene list (RendererScenes.hpp's DebugDrawingCSharpScene).</summary>
    public const uint Scene = 2;

    private const float Pi = 3.14159265f;
    // The groups the number keys show and hide, and the one F adds to and X clears.
    private static readonly string[] ToggledGroups = ["Wire", "Solid", "Labels"];
    private static readonly Key[] GroupKeys = [Key.Num1, Key.Num2, Key.Num3];
    private const string ForeverGroup = "Forever";
    // The two blocks of shapes, seven to a row, three rows high: the depth-tested block on the left and the on-top block
    // on the right, both behind a wall that hides their bottom row.
    private const float TestedBlockX = -21.0f;
    private const float OnTopBlockX = 3.0f;
    private const float Spacing = 3.0f;
    private const float RowHeight = 2.8f;
    private const int Columns = 7;

    // The entry spawned, and what the scene keeps between frames.
    private uint spawnedEntry;
    private Entity orbiter = Entity.None;
    private float seconds;
    private uint frames;
    private int foreverMarks;
    private bool circleDrawn;

    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(Scene, out uint entry))
            return;
        if (entry != spawnedEntry)
        {
            spawnedEntry = entry;
            Spawn(in context);
            // What was spawned plays back when this system returns, and the scene's debug drawing arrives next frame.
            return;
        }

        // Drawn once, kept forever and following the orbiter (relativeTo), so it moves with it and goes with it.
        if (!circleDrawn && orbiter.IsValid)
        {
            DebugDraw.Circle(Vector3.Zero, new Vector3(0, 1, 0), 0.9f, Colour.FromSrgb(0xF0D040FF),
                             new ShapeOptions { Group = "Orbiter", Duration = DebugDraw.Forever, RelativeTo = orbiter, Thickness = 3.0f });
            circleDrawn = true;
        }
        seconds += context.Time.DeltaTime;
        ++frames;

        // The orbiter circles above the rows; shapes relative to it follow it.
        float angle = seconds * 0.6f;
        if (orbiter.IsValid)
        {
            ref Transform transform = ref orbiter.Get<Transform>().Value;
            transform.Position = new Vector3(8.0f * MathF.Cos(angle), 10.0f, 3.0f * MathF.Sin(angle));
            transform.Rotation = AngleAxis(angle * 2.0f, new Vector3(0, 1, 0));
            DebugDraw.Axes(Matrix4x4.Identity, 1.5f, new ShapeOptions { Group = "Orbiter", RelativeTo = orbiter, Thickness = 2.0f });
            DebugDraw.Box(new Bounds(new Vector3(-0.6f, -0.6f, -0.6f), new Vector3(0.6f, 0.6f, 0.6f)), Colour.White,
                          new ShapeOptions { Group = "Orbiter", RelativeTo = orbiter });
            DebugDraw.WorldText(new Vector3(0, 1.2f, 0), "relativeTo the orbiter", Colour.FromSrgb(0xF0D040FF),
                                new TextOptions { Group = "Orbiter", RelativeTo = orbiter });
        }

        // Duration 0: drawn every frame, gone by the next.
        DrawBlock(TestedBlockX, DepthMode.Tested);
        DrawBlock(OnTopBlockX, DepthMode.OnTop);
        float titleX = 0.5f * Spacing * (Columns - 1);
        float titleY = 1.0f + 3.0f * RowHeight;
        DebugDraw.WorldText(new Vector3(TestedBlockX + titleX, titleY, 0), "Depth tested: the wall hides the bottom row",
                            Colour.White, new TextOptions { Size = 18.0f });
        DebugDraw.WorldText(new Vector3(OnTopBlockX + titleX, titleY, 0), "On top: drawn through the wall", Colour.White,
                            new TextOptions { Size = 18.0f });

        DebugDraw.ScreenText(new Vector2(16, 120),
                             FormattableString.Invariant(
                                 $"Screen text: frame {frames}, {seconds:0.0} s. Accents: café, naïve, Ærø. Not Latin-1: €"),
                             Colour.White);
        float degrees = (angle * 180.0f / Pi) % 360.0f;
        DebugDraw.Message(1, FormattableString.Invariant($"Keyed message 1: the orbiter is at {degrees:0} degrees"),
                          Colour.FromSrgb(0x80FF80FF));
        string groups = "Keyed message 2: groups";
        for (int i = 0; i < ToggledGroups.Length; ++i)
            groups += $" {i + 1}={ToggledGroups[i]} {(DebugDraw.IsGroupShown(ToggledGroups[i]) ? "on" : "off")}";
        DebugDraw.Message(2, groups, Colour.FromSrgb(0x80C0FFFF));

        for (int i = 0; i < GroupKeys.Length; ++i)
        {
            if (!Keyboard.JustPressed(GroupKeys[i]))
                continue;
            string group = ToggledGroups[i];
            if (DebugDraw.IsGroupShown(group))
                DebugDraw.HideGroup(group);
            else
                DebugDraw.ShowGroup(group);
            context.Log.Info($"{i + 1}: the {group} group is {(DebugDraw.IsGroupShown(group) ? "shown" : "hidden")}");
        }
        if (Keyboard.JustPressed(Key.T))
        {
            // A positive duration: three seconds, wherever the orbiter is now.
            Vector3 at = orbiter.IsValid ? orbiter.GetReadOnly<Transform>().Value.Position : new Vector3(8, 10, 0);
            Colour pink = Colour.FromSrgb(0xFF60C0FF);
            DebugDraw.Sphere(at, 1.2f, pink, new ShapeOptions { Duration = 3.0f, Thickness = 3.0f });
            DebugDraw.WorldText(at + new Vector3(0, 1.6f, 0), "3 seconds", pink, new TextOptions { Duration = 3.0f });
            DebugDraw.Message("T: a sphere for three seconds (an unkeyed message, for three seconds too)", pink,
                              new TextOptions { Duration = 3.0f });
            context.Log.Info("T: a sphere for three seconds");
        }
        if (Keyboard.JustPressed(Key.F))
        {
            var at = new Vector3(-15.0f + 3.0f * (foreverMarks % 11), 12.0f, -3.0f);
            ++foreverMarks;
            Colour green = Colour.FromSrgb(0xA0FF40FF);
            DebugDraw.Arrow(at + new Vector3(0, 1, 0), at, 0.3f, green,
                            new ShapeOptions { Group = ForeverGroup, Duration = DebugDraw.Forever, Thickness = 3.0f });
            DebugDraw.WorldText(at + new Vector3(0, 1.3f, 0), $"forever #{foreverMarks}", green,
                                new TextOptions { Group = ForeverGroup, Duration = DebugDraw.Forever });
            context.Log.Info($"F: forever mark {foreverMarks}");
        }
        if (Keyboard.JustPressed(Key.X))
        {
            DebugDraw.Clear(ForeverGroup);
            foreverMarks = 0;
            context.Log.Info($"X: the {ForeverGroup} group is cleared");
        }
    }

    private void Spawn(in SystemContext context)
    {
        orbiter = Entity.None;
        seconds = 0.0f;
        frames = 0;
        foreverMarks = 0;
        circleDrawn = false;

        // The scene owns what it loads and makes, so its assets go when it is left.
        AssetHandle<Mesh> cube = Assets.Load<Mesh>("cube.obj#Mesh0");
        AssetHandle<Mesh> sphere = Assets.Load<Mesh>("sphere.obj#Mesh0");
        if (!Assets.IsValid(cube) || !Assets.IsValid(sphere))
        {
            context.Log.Error("Debug draw (C#): the cube or sphere did not load from the asset root");
            return;
        }
        AssetHandle<Shader> lit = Assets.Load<Shader>("builtin:lit");

        // A floor, and a low wall in front of both rows: it hides the bottom of the depth-tested shapes, and the on-top
        // ones show through it.
        Place(cube, new Vector3(0.0f, -0.25f, 0.0f), new Vector3(50.0f, 0.5f, 12.0f), Make(lit, 0x6E6A64FF));
        Place(cube, new Vector3(0.0f, 0.75f, 2.0f), new Vector3(50.0f, 1.5f, 0.3f), Make(lit, 0xB05A3CFF));
        orbiter = Place(sphere, new Vector3(8.0f, 10.0f, 0.0f), new Vector3(0.5f, 0.5f, 0.5f), Make(lit, 0xF0D040FF));

        // The light shines along its -Z, down and away to the left.
        Entity light = Entity.Create();
        light.Add(new Transform { Position = Vector3.Zero, Rotation = LookAlong(new Vector3(-0.4f, -1.0f, -0.5f)), Scale = Vector3.One });
        light.Add(new DirectionalLight());
    }

    // A block of every wire and solid shape from `left`, seven to a row and a label over each, in `depth` mode; the
    // on-top block is drawn in warmer colours.
    private static void DrawBlock(float left, DepthMode depth)
    {
        bool onTop = depth == DepthMode.OnTop;
        Colour wire = onTop ? Colour.FromSrgb(0xFFE070FF) : Colour.FromSrgb(0x40E0FFFF);
        Colour solid = onTop ? new Colour(1.0f, 0.5f, 0.1f, 0.5f) : new Colour(0.2f, 0.6f, 1.0f, 0.5f);
        var options = new ShapeOptions { Group = "Wire", Thickness = 2.0f, Depth = depth };
        var solidOptions = new ShapeOptions { Group = "Solid", Depth = depth };
        var label = new TextOptions { Group = "Labels", Size = 14.0f };
        int index = 0;
        Vector3 Next(string name)
        {
            var at = new Vector3(left + Spacing * (index % Columns), 1.0f + RowHeight * (index / Columns), 0.0f);
            ++index;
            DebugDraw.WorldText(at + new Vector3(0, 1.25f, 0), name, Colour.White, label);
            return at;
        }

        Vector3 at = Next("Line");
        DebugDraw.Line(at - new Vector3(0.8f, 0.8f, 0), at + new Vector3(0.8f, 0.8f, 0), wire, options);
        at = Next("Ray");
        DebugDraw.Ray(at - new Vector3(0.8f, 0, 0), new Vector3(1.6f, 0.6f, 0), wire, options);
        at = Next("Arrow");
        DebugDraw.Arrow(at - new Vector3(0, 0.8f, 0), at + new Vector3(0, 0.8f, 0), 0.4f, wire, options);
        at = Next("Polyline");
        DebugDraw.Polyline([at + new Vector3(0, 0.9f, 0), at + new Vector3(0.55f, -0.75f, 0), at + new Vector3(-0.85f, 0.3f, 0),
                            at + new Vector3(0.85f, 0.3f, 0), at + new Vector3(-0.55f, -0.75f, 0)], true, wire, options);
        at = Next("Points");
        for (int i = 0; i < 3; ++i)
            DebugDraw.Point(at + new Vector3((i - 1) * 0.6f, 0, 0), 4.0f + 4.0f * i, wire, options);
        at = Next("Box");
        DebugDraw.Box(new Bounds(at - new Vector3(0.7f, 0.7f, 0.7f), at + new Vector3(0.7f, 0.7f, 0.7f)), wire, options);
        at = Next("Oriented box");
        DebugDraw.Box(at, new Vector3(0.8f, 0.5f, 0.4f), AngleAxis(0.6f, new Vector3(0, 0, 1)), wire, options);
        at = Next("Sphere");
        DebugDraw.Sphere(at, 0.8f, wire, options);
        at = Next("Capsule");
        DebugDraw.Capsule(at - new Vector3(0, 0.5f, 0), at + new Vector3(0, 0.5f, 0), 0.4f, wire, options);
        at = Next("Cylinder");
        DebugDraw.Cylinder(at - new Vector3(0, 0.7f, 0), at + new Vector3(0, 0.7f, 0), 0.5f, wire, options);
        at = Next("Cone");
        DebugDraw.Cone(at + new Vector3(0, 0.8f, 0), at - new Vector3(0, 0.8f, 0), 0.6f, wire, options);
        at = Next("Circle");
        DebugDraw.Circle(at, new Vector3(0, 0, 1), 0.8f, wire, options);
        at = Next("Arc");
        DebugDraw.Arc(at, new Vector3(0, 0, 1), new Vector3(0.8f, 0, 0), 1.5f * Pi, wire, options);
        at = Next("Plane");
        DebugDraw.Plane(at, Normalize(new Vector3(0, 0.3f, 1)), 0.7f, wire, options);
        at = Next("Grid");
        DebugDraw.Grid(at, AngleAxis(0.5f * Pi, new Vector3(1, 0, 0)), 4, 0.4f, wire, options);
        at = Next("Axes");
        DebugDraw.Axes(Translation(at), 0.8f, options);
        at = Next("Frustum");
        DebugDraw.Frustum(FrustumOf(at + new Vector3(0, 0, 0.9f)), wire, options, 0.25f);

        // Then the solids, alpha-blended.
        at = Next("Solid box");
        DebugDraw.SolidBox(new Bounds(at - new Vector3(0.6f, 0.6f, 0.6f), at + new Vector3(0.6f, 0.6f, 0.6f)), solid, solidOptions);
        at = Next("Solid sphere");
        DebugDraw.SolidSphere(at, 0.7f, solid, solidOptions);
        at = Next("Solid capsule");
        DebugDraw.SolidCapsule(at - new Vector3(0, 0.4f, 0), at + new Vector3(0, 0.4f, 0), 0.4f, solid, solidOptions);
        at = Next("Triangles");
        DebugDraw.Triangles([at + new Vector3(-0.8f, -0.6f, 0), at + new Vector3(0, -0.6f, 0), at + new Vector3(-0.4f, 0.6f, 0),
                             at + new Vector3(0, -0.6f, 0), at + new Vector3(0.8f, -0.6f, 0), at + new Vector3(0.4f, 0.6f, 0)],
                            solid, solidOptions);
    }

    // A reversed-Z perspective view-projection (ADR-0026) looking along -Z from `eye`, for a frustum: the C++ twin's
    // FrustumOf, its projection times a translation by -eye, by columns.
    private static Matrix4x4 FrustumOf(Vector3 eye)
    {
        const float near = 0.4f;
        float focal = 1.0f / MathF.Tan(0.5f * 0.6f);
        return new Matrix4x4(new Vector4(focal, 0, 0, 0), new Vector4(0, focal, 0, 0), new Vector4(0, 0, 0, -1),
                             new Vector4(-focal * eye.X, -focal * eye.Y, near, eye.Z));
    }

    // A move to `position`, by columns.
    private static Matrix4x4 Translation(Vector3 position) =>
        new(new Vector4(1, 0, 0, 0), new Vector4(0, 1, 0, 0), new Vector4(0, 0, 1, 0), new Vector4(position.X, position.Y, position.Z, 1));

    // A new builtin:lit material with its baseColour the sRGB colour `srgb`.
    private static AssetHandle<Material> Make(AssetHandle<Shader> shader, uint srgb)
    {
        AssetHandle<Material> made = Materials.Create(shader);
        Materials.SetColour(made, "baseColour", Colour.FromSrgb(srgb));
        return made;
    }

    // An entity drawing `mesh` with `material` at `position`, scaled by `scale`.
    private static Entity Place(AssetHandle<Mesh> mesh, Vector3 position, Vector3 scale, AssetHandle<Material> material)
    {
        Entity entity = Entity.Create();
        entity.Add(new Transform { Position = position, Rotation = Quaternion.Identity, Scale = scale });
        entity.Add(new MeshRenderer { Mesh = mesh, Material = material });
        return entity;
    }

    private static Vector3 Normalize(Vector3 v) => v / v.Length;

    // The turn that points -Z along `direction` with +Y up, as glm::quatLookAt does: a yaw about +Y after a pitch about +X.
    private static Quaternion LookAlong(Vector3 direction)
    {
        Vector3 d = Normalize(direction);
        return Multiply(AngleAxis(MathF.Atan2(-d.X, -d.Z), new Vector3(0, 1, 0)), AngleAxis(MathF.Asin(d.Y), new Vector3(1, 0, 0)));
    }

    private static Quaternion AngleAxis(float angle, Vector3 axis)
    {
        float s = MathF.Sin(angle * 0.5f);
        return new Quaternion(axis.X * s, axis.Y * s, axis.Z * s, MathF.Cos(angle * 0.5f));
    }

    private static Quaternion Multiply(Quaternion a, Quaternion b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
}
