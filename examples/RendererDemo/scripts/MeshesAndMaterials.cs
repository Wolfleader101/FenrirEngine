using Fenrir.Assets;
using Fenrir.Demo;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Renderer` and `Assets` name the classes.
namespace RendererDemo.Scripts;

/// <summary>
/// "Meshes and materials (C#)": scene 1, "Meshes and materials", built and run entirely from C# on the scripting
/// extensions (examples/RendererDemo/src/MeshesAndMaterials.cpp is its C++ twin, and the two must look the same). The
/// imported textured box, primitives in builtin:unlit and builtin:lit, a metallic/roughness grid, the four blend modes
/// and a double-sided quad, a user shader, the magenta fallback and an empty and a stale mesh; L turns the light, M
/// changes the ambient, V hides the textured box and C edits a shared material.
/// </summary>
[System(Phase.Update)]
public sealed class MeshesAndMaterials : ISystem
{
    /// <summary>The scene's number in RendererDemo's C++ scene list (RendererScenes.hpp's MeshesAndMaterialsCSharpScene).</summary>
    public const uint Scene = 1;

    private const float Pi = 3.14159265f;
    // The ambient light's intensities M cycles through: the default dim grey, bright, and off.
    private static readonly float[] AmbientSteps = [1.0f, 4.0f, 0.0f];
    // The shared material's colours C cycles through, sRGB.
    private static readonly uint[] SharedColours = [0xD4A017FF, 0x20B2AAFF, 0xC0203AFF];

    // The entry spawned, and what the keys change in it.
    private uint spawnedEntry;
    private Entity light = Entity.None;
    private Entity box = Entity.None;
    private Entity[] turning = [Entity.None, Entity.None];
    private AssetHandle<Material> shared;
    private float lightYaw;
    private int ambient;
    private int colour;
    private bool boxVisible;
    private float seconds;

    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(Scene, out uint entry))
            return;
        if (entry != spawnedEntry)
        {
            spawnedEntry = entry;
            Spawn(in context);
            // What was spawned plays back when this system returns.
            return;
        }

        seconds += context.Time.DeltaTime;
        foreach (Entity quad in turning)
            quad.Get<Transform>().Value.Rotation = YawPitch(seconds * 0.8f, 0.0f);

        if (Keyboard.JustPressed(Key.L))
        {
            lightYaw += Pi / 4.0f;
            light.Get<Transform>().Value.Rotation = YawPitch(lightYaw, -0.8f);
            context.Log.Info($"L: the light turned to {lightYaw * 180.0f / Pi:0} degrees about +Y");
        }
        if (Keyboard.JustPressed(Key.M))
        {
            ambient = (ambient + 1) % AmbientSteps.Length;
            Resources.Get<AmbientLight>().Value.Intensity = AmbientSteps[ambient];
            context.Log.Info($"M: the ambient light's intensity is {AmbientSteps[ambient]}");
        }
        if (Keyboard.JustPressed(Key.V))
        {
            boxVisible = !boxVisible;
            SetVisible(box, boxVisible);
            context.Log.Info($"V: the textured box's MeshRenderers are {(boxVisible ? "visible" : "hidden")}");
        }
        if (Keyboard.JustPressed(Key.C))
        {
            colour = (colour + 1) % SharedColours.Length;
            Materials.SetColour(shared, "baseColour", Colour.FromSrgb(SharedColours[colour]));
            context.Log.Info("C: the three cubes' shared material changed colour");
        }
    }

    private void Spawn(in SystemContext context)
    {
        lightYaw = 0.5f;
        ambient = 0;
        colour = 0;
        boxVisible = true;
        seconds = 0.0f;

        // The scene owns what it loads and makes, so its assets go when it is left.
        AssetHandle<Mesh> cube = Assets.Load<Mesh>("cube.obj#Mesh0");
        AssetHandle<Mesh> sphere = Assets.Load<Mesh>("sphere.obj#Mesh0");
        AssetHandle<Model> model = Assets.Load<Model>("BoxTextured/BoxTextured.gltf");
        if (!Assets.IsValid(cube) || !Assets.IsValid(sphere) || !Assets.IsValid(model))
        {
            context.Log.Error("Meshes and materials (C#): the cube, sphere or textured box did not load from the asset root");
            return;
        }
        AssetHandle<Shader> lit = Assets.Load<Shader>("builtin:lit");
        AssetHandle<Shader> unlit = Assets.Load<Shader>("builtin:unlit");

        // The ground.
        Place(cube, Make(lit, 0x606060FF), new Vector3(0.0f, -0.1f, 0.0f), new Vector3(22.0f, 0.2f, 14.0f));

        // The imported textured box (Khronos's BoxTextured), spawned as a node tree of MeshRenderers; its root is
        // usable at once.
        box = Renderer.SpawnModel(model);
        box.Add(new Transform
        {
            Position = new Vector3(-7.6f, 0.75f, -0.5f),
            Rotation = YawPitch(0.6f, 0.0f),
            Scale = new Vector3(1.5f, 1.5f, 1.5f),
        });

        // Primitives in builtin:unlit, then in builtin:lit.
        Place(cube, Make(unlit, 0x3070E0FF), new Vector3(-4.2f, 0.5f, 0.0f));
        Place(sphere, Make(unlit, 0x3070E0FF), new Vector3(-3.0f, 0.5f, 0.0f));
        Place(cube, Make(lit, 0x3070E0FF), new Vector3(-1.6f, 0.5f, 0.0f));
        Place(sphere, Make(lit, 0x3070E0FF), new Vector3(-0.4f, 0.5f, 0.0f));

        // A user's own shader, from RendererDemo's sources, on a cube: the app embeds it, and names it for C#.
        AssetHandle<Shader> stripes = Assets.Load<Shader>("builtin:renderer-demo-stripes");
        Place(cube, Materials.Create(stripes), new Vector3(1.0f, 0.5f, 0.0f));

        // The fallback: a MeshRenderer with no material draws flat magenta, logged once.
        Place(cube, AssetHandle<Material>.None, new Vector3(2.4f, 0.5f, 0.0f));
        // An empty mesh and a stale one beside it draw nothing; the stale one is logged once.
        Place(AssetHandle<Mesh>.None, Make(lit, 0xFFFFFFFF), new Vector3(3.6f, 0.5f, 0.0f));
        AssetHandle<Mesh> stale = Quad(1.0f, 1.0f);
        Assets.Unload(stale);
        Place(stale, Make(lit, 0xFFFFFFFF), new Vector3(3.6f, 0.5f, 0.0f));

        // Three cubes sharing one material, which C edits.
        shared = Make(lit, SharedColours[0]);
        foreach (float x in new[] { 4.8f, 6.0f, 7.2f })
            Place(cube, shared, new Vector3(x, 0.5f, 0.0f));

        // The metallic/roughness grid: metallic 0 to 1 from left to right, roughness 0.1 to 1 from bottom to top.
        Colour gold = Colour.FromSrgb(0xE0B060FF);
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 5; ++column)
            {
                AssetHandle<Material> ball = Materials.Create(lit);
                Materials.SetColour(ball, "baseColour", gold);
                Materials.SetFloat(ball, "metallic", column / 4.0f);
                Materials.SetFloat(ball, "roughness", 0.1f + 0.3f * row);
                Place(sphere, ball, new Vector3(3.0f + 1.2f * column, 1.0f + 1.2f * row, -4.0f), new Vector3(0.9f, 0.9f, 0.9f));
            }
        }

        // The four blend modes side by side in front of a checkerboard: Opaque, Masked, AlphaBlend and Additive, each a
        // disc fading out to its edge.
        var nearest = new SamplerDescription { Filter = SamplerFilter.Nearest };
        AssetHandle<Material> board = Make(lit, 0xFFFFFFFF);
        Materials.SetTexture(board, "baseColourTexture", Assets.AddImage(8, 8, Checker()), nearest);
        Place(Quad(7.0f, 2.0f, 4.0f), board, new Vector3(-2.5f, 2.2f, -2.4f));
        AssetHandle<Image> alphaDisc = Assets.AddImage(64, 64, Disc(64, inAlpha: true));
        AssetHandle<Image> glowDisc = Assets.AddImage(64, 64, Disc(64, inAlpha: false));
        AssetHandle<Mesh> discQuad = Quad(1.4f, 1.4f);
        (BlendMode Mode, uint Srgb)[] modes =
        [
            (BlendMode.Opaque, 0xE04040FF),
            (BlendMode.Masked, 0x40C040FF),
            (BlendMode.AlphaBlend, 0x4060E0FF),
            (BlendMode.Additive, 0xE0A020FF),
        ];
        for (int i = 0; i < modes.Length; ++i)
        {
            AssetHandle<Material> disc = Make(unlit, modes[i].Srgb);
            Materials.SetBlendMode(disc, modes[i].Mode);
            Materials.SetTexture(disc, "baseColourTexture", modes[i].Mode == BlendMode.Additive ? glowDisc : alphaDisc);
            Place(discQuad, disc, new Vector3(-5.1f + 1.75f * i, 2.2f, -2.0f));
        }

        // A double-sided quad turning about +Y, and a single-sided one beside it, which vanishes while it faces away.
        AssetHandle<Mesh> turningQuad = Quad(1.2f, 1.2f);
        AssetHandle<Material> twoSided = Make(lit, 0x8040C0FF);
        Materials.SetDoubleSided(twoSided, true);
        turning = [Place(turningQuad, twoSided, new Vector3(8.6f, 0.8f, 0.0f)),
                   Place(turningQuad, Make(lit, 0x8040C0FF), new Vector3(10.2f, 0.8f, 0.0f))];

        // The light shines along its -Z; L turns it about +Y.
        light = Entity.Create();
        light.Add(new Transform { Position = Vector3.Zero, Rotation = YawPitch(lightYaw, -0.8f), Scale = Vector3.One });
        light.Add(new DirectionalLight());
        Resources.Insert(new AmbientLight());
    }

    // A new material on `shader` with its baseColour the sRGB colour `srgb`.
    private static AssetHandle<Material> Make(AssetHandle<Shader> shader, uint srgb)
    {
        AssetHandle<Material> made = Materials.Create(shader);
        Materials.SetColour(made, "baseColour", Colour.FromSrgb(srgb));
        return made;
    }

    // An entity drawing `mesh` with `material` at `position`, scaled by `scale` (one by default).
    private static Entity Place(AssetHandle<Mesh> mesh, AssetHandle<Material> material, Vector3 position, Vector3? scale = null)
    {
        Entity entity = Entity.Create();
        entity.Add(new Transform { Position = position, Rotation = Quaternion.Identity, Scale = scale ?? Vector3.One });
        entity.Add(new MeshRenderer { Mesh = mesh, Material = material });
        return entity;
    }

    // A quad of `width` x `height` in the XY plane facing +Z, with UV (0, 0) at its top-left corner and `repeat` at its
    // bottom-right.
    private static AssetHandle<Mesh> Quad(float width, float height, float repeat = 1.0f)
    {
        float w = width * 0.5f;
        float h = height * 0.5f;
        ReadOnlySpan<Vector3> positions = [new(-w, -h, 0.0f), new(w, -h, 0.0f), new(w, h, 0.0f), new(-w, h, 0.0f)];
        ReadOnlySpan<Vector3> normals = [new(0, 0, 1), new(0, 0, 1), new(0, 0, 1), new(0, 0, 1)];
        ReadOnlySpan<Vector2> uvs = [new(0.0f, repeat), new(repeat, repeat), new(repeat, 0.0f), new(0.0f, 0.0f)];
        ReadOnlySpan<uint> indices = [0, 1, 2, 0, 2, 3];
        return Assets.AddMesh(positions, normals, uvs, indices);
    }

    // A `size` x `size` sRGB image of a white disc fading out to its edge: in alpha over white for `inAlpha`, else in
    // colour over opaque black.
    private static byte[] Disc(int size, bool inAlpha)
    {
        var pixels = new byte[size * size * 4];
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float u = (x + 0.5f) / size * 2.0f - 1.0f;
                float v = (y + 0.5f) / size * 2.0f - 1.0f;
                float fade = System.Math.Clamp(1.0f - MathF.Sqrt(u * u + v * v), 0.0f, 1.0f);
                byte level = (byte)MathF.Round(fade * 255.0f, MidpointRounding.AwayFromZero);
                int at = (y * size + x) * 4;
                if (inAlpha)
                {
                    pixels[at] = 255;
                    pixels[at + 1] = 255;
                    pixels[at + 2] = 255;
                    pixels[at + 3] = level;
                }
                else
                {
                    pixels[at] = level;
                    pixels[at + 1] = level;
                    pixels[at + 2] = level;
                    pixels[at + 3] = 255;
                }
            }
        }
        return pixels;
    }

    // An 8 x 8 checkerboard of two sRGB greys.
    private static byte[] Checker()
    {
        var pixels = new byte[8 * 8 * 4];
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                byte level = (x + y) % 2 == 0 ? (byte)220 : (byte)60;
                int at = (y * 8 + x) * 4;
                pixels[at] = level;
                pixels[at + 1] = level;
                pixels[at + 2] = level;
                pixels[at + 3] = 255;
            }
        }
        return pixels;
    }

    // Shows or hides every MeshRenderer at or below `entity`.
    private static void SetVisible(Entity entity, bool visible)
    {
        if (entity.Has<MeshRenderer>())
            entity.Get<MeshRenderer>().Value.Visible = visible;
        Entity child = entity.GetReadOnly<Relationship>().Value.FirstChild;
        while (child != Entity.None)
        {
            SetVisible(child, visible);
            child = child.GetReadOnly<Relationship>().Value.NextSibling;
        }
    }

    // A turn of `yaw` radians about +Y after `pitch` radians about +X, as the C++ twin's YawPitch.
    private static Quaternion YawPitch(float yaw, float pitch) =>
        Multiply(AngleAxis(yaw, new Vector3(0, 1, 0)), AngleAxis(pitch, new Vector3(1, 0, 0)));

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
