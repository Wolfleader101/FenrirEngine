using Fenrir.Assets;
using Fenrir.Demo;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

namespace LightingDemo.Scripts;

// MANY LIGHTS (C#): THE MANY-LIGHTS ROOM, MADE AND RUN FROM C#
//
// The twin of Many lights (src/ManyLights.cpp): the same dark room of PBR floor tiles, walls, columns and spheres, lit by
// up to 1024 moving coloured point and spot lights (every fourth a spot), made here from Fenrir.Renderer's components.
// Where C++ marks each light with a sphere entity, this scene draws every marker as one copy of an instance list, made
// with Assets.AddInstanceList and rewritten each frame, through one InstancedMeshRenderer. The scaffold gives its
// FenrirScene a CSharpScene resource naming it (src/ManyLights.cpp, ManyLightsCSharpScene); the lighting panel's keys
// (T, E, B, O, G) are C++'s, as in every LightingDemo scene. Every system here acts only while the scene is active.
//
//   M   16, 64, 100, 256 or 1024 lights.
//   P   the lights moving or still.
//   K   spot shadows off or on.
//   J   point shadows off or on.
//   V   the shadow budgets: 8 spot and 4 point (the defaults), 16 and 8, then none (Renderer.SetSpotShadowBudget and
//       SetPointShadowBudget).

// The scene's own marker, on one entity, so it is built once each time it is entered.
public struct ManyLightsSceneMarker : IComponent
{
    public int Built;
}

// One moving light: its entity, whether it is a spot, its colour, and its orbit about the room's centre.
public struct MovingLight
{
    public Entity Entity;
    public bool Spot;
    public Colour Colour;
    public float Radius;
    public float Height;
    public float Speed;
    public float Phase;
}

public static class ManyLights
{
    // This scene's number in its CSharpScene, as C++ gives it (ManyLightsCSharpScene in src/ManyLights.cpp).
    public const uint SceneId = 1;

    public const int MaxLights = 1024;
    public static readonly int[] CountSteps = [16, 64, 100, 256, 1024];
    // How many hues the lights take, as in C++.
    public const int Hues = 32;
    public const float HalfRoom = 10.0f;
    public const int SpotEvery = 4;
    public const float MarkerSize = 0.12f;
    // The shadow budgets V steps through: spot, then point.
    public static readonly (uint Spot, uint Point)[] Budgets = [(8, 4), (16, 8), (0, 0)];

    // The scene's state, made again each time it is built.
    public static MovingLight[] Lights = [];
    public static AssetHandle<InstanceList> Markers;
    public static int Count = 100;
    public static bool Moving = true;
    public static float Time;
    public static float PointIntensity = 2.0f;
    public static float SpotIntensity = 8.0f;
    public static float Range = 5.0f;
    public static bool SpotShadows = true;
    public static bool PointShadows = true;
    public static int Budget;
    // The markers' copies, written for the lit lights each frame: one buffer, so a frame allocates nothing.
    private static readonly Matrix4x4[] markerTransforms = new Matrix4x4[MaxLights];
    private static readonly Colour[] markerColours = new Colour[MaxLights];

    // Whether the active scene is this one, built.
    public static bool IsActive(in SystemContext context)
    {
        if (!CSharpScene.IsActive(SceneId, out _))
            return false;
        foreach (var (_, marker) in context.Query<ManyLightsSceneMarker>())
            if (marker.Built != 0)
                return true;
        return false;
    }

    // Hue `hue` (0 to 1 round the wheel) at full saturation and value, linear.
    public static Colour HueColour(float hue)
    {
        float Channel(float offset)
        {
            float k = (hue * 6 + offset) % 6;
            return 1 - MathF.Max(0, MathF.Min(MathF.Min(k, 4 - k), 1));
        }
        return new Colour(Channel(5), Channel(3), Channel(1), 1);
    }

    public static Vector3 Normalize(Vector3 value) => value / MathF.Sqrt(Vector3.Dot(value, value));

    public static Vector3 Cross(Vector3 a, Vector3 b) =>
        new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);

    // The rotation that turns -Z to `direction` with +Y towards `up`, as glm::quatLookAt (right-handed) makes it.
    public static Quaternion LookAt(Vector3 direction, Vector3 up)
    {
        Vector3 back = -Normalize(direction);
        Vector3 right = Normalize(Cross(up, back));
        Vector3 top = Cross(back, right);
        // The rotation matrix's columns are right, top and back; its quaternion, by the largest diagonal term.
        float trace = right.X + top.Y + back.Z;
        if (trace > 0)
        {
            float s = MathF.Sqrt(trace + 1) * 2;
            return new Quaternion((top.Z - back.Y) / s, (back.X - right.Z) / s, (right.Y - top.X) / s, s / 4);
        }
        if (right.X > top.Y && right.X > back.Z)
        {
            float s = MathF.Sqrt(1 + right.X - top.Y - back.Z) * 2;
            return new Quaternion(s / 4, (top.X + right.Y) / s, (back.X + right.Z) / s, (top.Z - back.Y) / s);
        }
        if (top.Y > back.Z)
        {
            float s = MathF.Sqrt(1 + top.Y - right.X - back.Z) * 2;
            return new Quaternion((top.X + right.Y) / s, s / 4, (back.Y + top.Z) / s, (back.X - right.Z) / s);
        }
        float t = MathF.Sqrt(1 + back.Z - right.X - top.Y) * 2;
        return new Quaternion((back.X + right.Z) / t, (back.Y + top.Z) / t, t / 4, (right.Y - top.X) / t);
    }

    // Where `light` is at `time`, and for a spot, the way it points.
    public static Transform Place(in MovingLight light, float time)
    {
        float angle = light.Phase + light.Speed * time;
        float bob = light.Spot ? 0 : 0.3f * MathF.Sin(time * 1.3f + light.Phase * 3);
        var transform = new Transform
        {
            Position = new Vector3(MathF.Cos(angle) * light.Radius, light.Height + bob, MathF.Sin(angle) * light.Radius),
            Rotation = Quaternion.Identity,
            Scale = Vector3.One,
        };
        // Down, leaning out from the room's centre and swinging round as it goes.
        if (light.Spot)
            transform.Rotation = LookAt(new Vector3(MathF.Cos(angle * 2) * 0.5f, -1, MathF.Sin(angle * 2) * 0.5f),
                new Vector3(1, 0, 0));
        return transform;
    }

    // Light `i`'s place at `Time`, and its intensity (none past the count), range and shadows as the keys set them.
    public static void Apply(int i)
    {
        ref MovingLight light = ref Lights[i];
        bool on = i < Count;
        light.Entity.Get<Transform>().Value = Place(light, Time);
        if (light.Spot)
        {
            ref SpotLight spot = ref light.Entity.Get<SpotLight>().Value;
            spot.Intensity = on ? SpotIntensity : 0;
            spot.Range = Range + 2;
            spot.CastShadows = SpotShadows;
        }
        else
        {
            ref PointLight point = ref light.Entity.Get<PointLight>().Value;
            point.Intensity = on ? PointIntensity : 0;
            point.Range = Range;
            point.CastShadows = PointShadows;
        }
    }

    // The markers of the first `Count` lights at `Time`: a small sphere at each light, coloured by it. The list holds as
    // many copies as there are lights on. With no list yet (Build's call), it makes one.
    public static void WriteMarkers()
    {
        for (int i = 0; i < Count; ++i)
        {
            Vector3 at = Place(Lights[i], Time).Position;
            markerTransforms[i] = new Matrix4x4(new Vector4(MarkerSize, 0, 0, 0), new Vector4(0, MarkerSize, 0, 0),
                new Vector4(0, 0, MarkerSize, 0), new Vector4(at.X, at.Y, at.Z, 1));
            markerColours[i] = Lights[i].Colour;
        }
        ReadOnlySpan<Matrix4x4> transforms = markerTransforms.AsSpan(0, Count);
        ReadOnlySpan<Colour> colours = markerColours.AsSpan(0, Count);
        if (Assets.IsValid(Markers))
            Assets.ReplaceInstanceList(Markers, transforms, colours);
        else
            Markers = Assets.AddInstanceList(transforms, colours);
    }
}

// Builds the scene once each time it is entered: the room as entities, every light as an entity (those past the count
// dark, so the count spawns nothing), and the markers as one instance list.
[System(Phase.Update, Order = 100)]
public sealed class ManyLightsBuilder : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(ManyLights.SceneId, out _))
            return;
        foreach (var _ in context.Query<ManyLightsSceneMarker>())
            return; // Built already, this entry.
        Build(in context);
    }

    private static AssetHandle<Material> Lit(AssetHandle<Shader> lit, Colour colour, float metallic, float roughness)
    {
        AssetHandle<Material> material = Materials.Create(lit);
        Materials.SetColour(material, "baseColour", colour);
        Materials.SetFloat(material, "metallic", metallic);
        Materials.SetFloat(material, "roughness", roughness);
        return material;
    }

    private static void Draw(AssetHandle<Mesh> mesh, AssetHandle<Material> material, Vector3 position, Vector3 scale)
    {
        Entity entity = Entity.Create();
        entity.Add(new Transform { Position = position, Rotation = Quaternion.Identity, Scale = scale });
        entity.Add(new MeshRenderer { Mesh = mesh, Material = material });
    }

    private static void Build(in SystemContext context)
    {
        // Nothing of the last entry's: its entities and assets went with its FenrirScene.
        ManyLights.Lights = [];
        ManyLights.Markers = default;
        Entity marker = Entity.Create();
        var label = new Name();
        label.SetText("Many lights (C#) scene");
        marker.Add(label);
        marker.Add(new ManyLightsSceneMarker { Built = 1 });

        // A dark room: the lights are what shows.
        Sky sky = new ColourSky { Colour = new Colour(0.002f, 0.002f, 0.003f, 1) };
        if (Resources.TryGet(out Ref<Sky> held))
            held.Value = sky;
        else
            Resources.Insert(sky);

        AssetHandle<Mesh> cube = Assets.Load<Mesh>("cube.obj#Mesh0");
        AssetHandle<Mesh> sphere = Assets.Load<Mesh>("sphere.obj#Mesh0");
        if (!Assets.IsValid(cube) || !Assets.IsValid(sphere))
        {
            // Built with no lights, so the updater has nothing to move, as C++ keeps its state with none.
            context.Log.Error("Many lights (C#): the cube or the sphere did not load from the asset root");
            return;
        }
        AssetHandle<Shader> lit = Assets.Load<Shader>("builtin:lit");

        // Five by five tiles, rough to smooth across and dielectric to metal back to front, all a light grey.
        const int Tiles = 5;
        const float Tile = 2 * ManyLights.HalfRoom / Tiles;
        for (int z = 0; z < Tiles; ++z)
        {
            for (int x = 0; x < Tiles; ++x)
            {
                AssetHandle<Material> tile = Lit(lit, new Colour(0.6f, 0.6f, 0.6f, 1), (float)z / (Tiles - 1),
                    0.1f + 0.8f * x / (Tiles - 1));
                Draw(cube, tile, new Vector3(-ManyLights.HalfRoom + Tile * (x + 0.5f), -0.1f,
                    -ManyLights.HalfRoom + Tile * (z + 0.5f)), new Vector3(Tile, 0.2f, Tile));
            }
        }

        AssetHandle<Material> wall = Lit(lit, new Colour(0.8f, 0.78f, 0.75f, 1), 0, 0.8f);
        const float WallHeight = 4;
        const float Side = 2 * ManyLights.HalfRoom;
        Draw(cube, wall, new Vector3(0, WallHeight / 2, -ManyLights.HalfRoom), new Vector3(Side, WallHeight, 0.2f));
        Draw(cube, wall, new Vector3(0, WallHeight / 2, ManyLights.HalfRoom), new Vector3(Side, WallHeight, 0.2f));
        Draw(cube, wall, new Vector3(-ManyLights.HalfRoom, WallHeight / 2, 0), new Vector3(0.2f, WallHeight, Side));
        Draw(cube, wall, new Vector3(ManyLights.HalfRoom, WallHeight / 2, 0), new Vector3(0.2f, WallHeight, Side));

        // A ring of columns, alternately polished metal and rough plastic, and spheres between them.
        AssetHandle<Material> metal = Lit(lit, new Colour(0.95f, 0.93f, 0.88f, 1), 1, 0.25f);
        AssetHandle<Material> plastic = Lit(lit, new Colour(0.2f, 0.25f, 0.7f, 1), 0, 0.6f);
        AssetHandle<Material> gold = Lit(lit, new Colour(1, 0.77f, 0.34f, 1), 1, 0.4f);
        AssetHandle<Material> white = Lit(lit, new Colour(0.9f, 0.9f, 0.9f, 1), 0, 0.15f);
        const int Columns = 8;
        for (int i = 0; i < Columns; ++i)
        {
            float angle = 2 * MathF.PI * i / Columns;
            Draw(cube, i % 2 == 0 ? metal : plastic, new Vector3(MathF.Cos(angle) * 5, 1.5f, MathF.Sin(angle) * 5),
                new Vector3(0.6f, 3, 0.6f));
            float between = angle + MathF.PI / Columns;
            Draw(sphere, i % 2 == 0 ? gold : white, new Vector3(MathF.Cos(between) * 5, 0.6f, MathF.Sin(between) * 5),
                new Vector3(1.2f, 1.2f, 1.2f));
        }

        // Every light, as C++ makes them: the golden ratio spreads neighbours' hues and paths apart.
        ManyLights.Lights = new MovingLight[ManyLights.MaxLights];
        ManyLights.Count = 100;
        ManyLights.Moving = true;
        ManyLights.Time = 0;
        ManyLights.SpotShadows = true;
        ManyLights.PointShadows = true;
        ManyLights.Budget = 0;
        Renderer.SetSpotShadowBudget(ManyLights.Budgets[0].Spot);
        Renderer.SetPointShadowBudget(ManyLights.Budgets[0].Point);
        for (int i = 0; i < ManyLights.MaxLights; ++i)
        {
            float spread = i * 0.618034f % 1;
            int hue = (int)(spread * ManyLights.Hues);
            bool spot = i % ManyLights.SpotEvery == ManyLights.SpotEvery - 1;
            var light = new MovingLight
            {
                Entity = Entity.Create(),
                Spot = spot,
                Colour = ManyLights.HueColour((float)hue / ManyLights.Hues),
                Radius = 1.5f + 7.5f * (i * 0.381966f % 1),
                Height = spot ? 3.5f : 0.4f + 1.6f * spread,
                Speed = (0.15f + 0.35f * spread) * (i % 2 == 0 ? 1 : -1),
                Phase = 2 * MathF.PI * spread,
            };
            // Their place, intensity, range and shadows are the updater's (ManyLights.Apply), from this frame on.
            light.Entity.Add(ManyLights.Place(light, 0));
            if (spot)
                light.Entity.Add(new SpotLight { Colour = light.Colour, InnerAngle = 0.25f, OuterAngle = 0.5f });
            else
                light.Entity.Add(new PointLight { Colour = light.Colour });
            ManyLights.Lights[i] = light;
        }

        // Each light marked by a small sphere of its colour, bright enough to bloom: one copy of the markers' list each,
        // the unlit material's colour of 4 times its hue. It casts no shadow: round the light, it would shadow all of a
        // spot light's cone, or everything a point light lights.
        ManyLights.WriteMarkers();
        AssetHandle<Material> glow = Materials.Create(Assets.Load<Shader>("builtin:unlit"));
        Materials.SetColour(glow, "baseColour", new Colour(4, 4, 4, 1));
        Entity markers = Entity.Create();
        markers.Add(new Transform { Rotation = Quaternion.Identity, Scale = Vector3.One });
        markers.Add(new InstancedMeshRenderer
        {
            Mesh = sphere,
            Material = glow,
            Instances = ManyLights.Markers,
            CastShadows = false,
        });

        context.Log.Info($"Many lights (C#): C# built the room, {ManyLights.MaxLights} lights ({ManyLights.Count} on) " +
                         "and their markers as one instance list.");
    }
}

// The scene's keys, and each frame the lights' places and intensities, and the markers' list rewritten.
[System(Phase.Update, Order = 110)]
public sealed class ManyLightsUpdater : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!ManyLights.IsActive(in context))
            return;

        if (Keyboard.JustPressed(Key.M))
        {
            int next = Array.Find(ManyLights.CountSteps, step => step > ManyLights.Count);
            ManyLights.Count = next == 0 ? ManyLights.CountSteps[0] : next;
            context.Log.Info($"M: {ManyLights.Count} lights");
        }
        if (Keyboard.JustPressed(Key.P))
        {
            ManyLights.Moving = !ManyLights.Moving;
            context.Log.Info($"P: the lights are {(ManyLights.Moving ? "moving" : "still")}");
        }
        if (Keyboard.JustPressed(Key.K))
        {
            ManyLights.SpotShadows = !ManyLights.SpotShadows;
            context.Log.Info($"K: spot shadows {(ManyLights.SpotShadows ? "on" : "off")}");
        }
        if (Keyboard.JustPressed(Key.J))
        {
            ManyLights.PointShadows = !ManyLights.PointShadows;
            context.Log.Info($"J: point shadows {(ManyLights.PointShadows ? "on" : "off")}");
        }
        if (Keyboard.JustPressed(Key.V))
        {
            ManyLights.Budget = (ManyLights.Budget + 1) % ManyLights.Budgets.Length;
            var (spot, point) = ManyLights.Budgets[ManyLights.Budget];
            Renderer.SetSpotShadowBudget(spot);
            Renderer.SetPointShadowBudget(point);
            context.Log.Info($"V: shadow budgets of {spot} spot and {point} point lights");
        }

        if (ManyLights.Moving)
            ManyLights.Time += context.Time.DeltaTime;
        if (ManyLights.Lights.Length == 0)
            return; // Built without its meshes: there is nothing to move.
        for (int i = 0; i < ManyLights.Lights.Length; ++i)
            ManyLights.Apply(i);
        ManyLights.WriteMarkers();
    }
}
