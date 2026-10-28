using Fenrir.Assets;
using Fenrir.Demo;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Renderer;
using Fenrir.Scripting;

namespace PhysicsDemo.Scripts;

// PLAYGROUND (C#): A PHYSICS SCENE MADE AND RUN ONLY FROM C#
//
// The legacy DemoApp's "Physics (C#)" scene. The scaffold gives its FenrirScene a CSharpScene resource naming it
// (src/Playground.cpp, PlaygroundCSharpScene); everything in it is made here: the bodies, from the Fenrir.Physics
// components; what they look like, from Fenrir.Renderer's MeshRenderer (Looks.cs); and what happens, from the physics
// events and the PhysicsWorld functions. Every system here acts only while the scene is active.
//
//   J            kicks every box and sphere into the air (PhysicsWorld.AddImpulse).
//   Left click   casts a ray from the camera through the click (PhysicsWorld.Raycast), logs what it hit and pokes it.
//   F2           cycles the physics debug lines, as in every PhysicsDemo scene (src/SceneLooks.cpp).

// The scene's own marker, on one entity: Built once the builder has made it, so it is built once each time it is entered,
// even across a hot reload.
public struct PlaygroundScene : IComponent
{
    public int Built;
}

// The kinematic paddle's sweep: x goes from -Reach to Reach and back, Speed radians of the cycle per second.
public struct Paddle : IComponent
{
    public float Reach;
    public float Speed;
    public float Time; // Seconds of sweeping so far.
}

// What the scene's bodies have in common: their own material, the paler one they wear while in a trigger zone, and how
// many trigger zones they are in.
public struct Tint : IComponent
{
    public AssetHandle<Material> Base;
    public AssetHandle<Material> Lit;
    public int Inside;
}

public static class Playground
{
    // This scene's number in its CSharpScene, as C++ gives it (PlaygroundCSharpScene in src/PhysicsScenes.hpp).
    public const uint SceneId = 1;

    // Collision layers, one bit each. Bodies are in layer 0 unless given a CollisionFilter. The fixtures (the ground and
    // the paddle) are in their own layer, so the trigger zone can leave them out: it only reports the boxes and spheres.
    public const uint FixturesLayer = 1u << 1;
    public const uint ZoneLayer = 1u << 2;

    // Whether the active scene is this one, built.
    public static bool IsActive(in SystemContext context)
    {
        if (!CSharpScene.IsActive(SceneId, out _))
            return false;
        foreach (var (entity, marker) in context.Query<PlaygroundScene>())
            if (marker.Built != 0)
                return true;
        return false;
    }

    public static string NameOf(Entity entity) => entity.IsValid ? entity.Get<Name>().Value.GetText() : "(destroyed)";

    public static string Format(Vector3 value) => $"({value.X:0.00}, {value.Y:0.00}, {value.Z:0.00})";

    // A body's colour while it is in a trigger zone: most of the way to white.
    public static Vector3 Lit(Vector3 colour) => colour * 0.35f + new Vector3(1, 1, 0.8f) * 0.65f;
}

// Builds the scene once each time it is entered.
[System(Phase.Update, Order = 100)]
public sealed class PlaygroundBuilder : ISystem
{
    // Colours a quarter of a turn of hue apart and then some (the golden ratio), so neighbours never look alike.
    private float hue;

    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(Playground.SceneId, out _))
            return;
        foreach (var (entity, marker) in context.Query<PlaygroundScene>())
            return; // Built already, this entry.
        Build(in context);
    }

    private void Build(in SystemContext context)
    {
        hue = 0.1f;
        Looks.Begin(in context, "Playground (C#)");
        Entity marker = Entity.Create();
        var label = new Name();
        label.SetText("Playground (C#) scene");
        marker.Add(label);
        marker.Add(new PlaygroundScene { Built = 1 });

        // A collider with no RigidBody is static. The ground's top face is at y = -3.75.
        Entity ground = Make("Ground", new Vector3(0, -4, -12), new Vector3(16, 0.5f, 16), sphere: false,
            new Vector3(0.45f, 0.45f, 0.45f));
        ground.Add(new Collider());
        ground.Add(new CollisionFilter { Layers = Playground.FixturesLayer });

        // A pyramid of boxes, four on the bottom row.
        int boxes = 0;
        for (int row = 0; row < 4; row++)
        {
            for (int column = 0; column < 4 - row; column++)
            {
                float x = -1.575f + 1.05f * column + 0.525f * row;
                Entity box = Make($"Box {++boxes}", new Vector3(x, -3.25f + 1.0f * row, -12), Vector3.One, sphere: false,
                    NextColour());
                box.Add(new RigidBody());
                box.Add(new Collider());
            }
        }

        // Spheres of different sizes dropped onto it. Bouncy, and they report their contacts (logged by PlaygroundEvents).
        float[] sizes = [0.7f, 1.0f, 1.3f, 0.8f, 1.1f];
        for (int index = 0; index < sizes.Length; index++)
        {
            float size = sizes[index];
            Entity ball = Make($"Sphere {index + 1}", new Vector3(-2.4f + 1.2f * index, 3 + 1.5f * index, -11.5f + 0.4f * index),
                new Vector3(size, size, size), sphere: true, NextColour());
            ball.Add(new RigidBody());
            ball.Add(new Collider(new SphereShape())); // Radius 0.5, scaled by the Transform like the mesh.
            ball.Add(new PhysicsMaterial { Restitution = 0.6f });
            ball.Add(new ContactEvents());
        }

        // Kinematic: moved by PaddleMover, pushes the bodies and is never pushed.
        Entity paddle = Make("Paddle", new Vector3(-7, -3.2f, -12), new Vector3(1, 1, 4), sphere: false,
            new Vector3(1.0f, 0.55f, 0.0f));
        paddle.Add(new RigidBody { MotionType = MotionType.Kinematic });
        paddle.Add(new Collider());
        paddle.Add(new CollisionFilter { Layers = Playground.FixturesLayer });
        paddle.Add(new Paddle { Reach = 7, Speed = 0.5f });

        // A trigger zone at the end of the paddle's sweep: a thin pad on the ground, its trigger reaching a metre above
        // and below it (half extents are before scale, so 10 times the pad's 0.1 is 1 each way). Static, as it has no
        // RigidBody. Its mask leaves out the fixtures' layer, so the ground it sinks into and the paddle sweeping through
        // it are not reported.
        Entity zone = Make("Trigger zone", new Vector3(5, -3.7f, -12), new Vector3(3, 0.1f, 5), sphere: false,
            new Vector3(0.1f, 0.8f, 0.3f));
        zone.Add(new Collider(new BoxShape { HalfExtents = new Vector3(0.5f, 10, 0.5f) }));
        zone.Add(new Trigger());
        zone.Add(new CollisionFilter { Layers = Playground.ZoneLayer, Mask = ~Playground.FixturesLayer });

        // Its keys are logged by the scaffold's help (F1), from src/Playground.cpp.
        context.Log.Info($"Physics (C#): C# built a ground, {boxes} boxes, {sizes.Length} spheres, a paddle and a trigger " +
                         "zone.");
    }

    // An entity drawn as the cube or the sphere in `colour`, with the paler material it wears in a trigger zone. Its
    // components land when this system returns.
    private static Entity Make(string text, Vector3 position, Vector3 scale, bool sphere, Vector3 colour)
    {
        Entity entity = Entity.Create();
        var name = new Name();
        name.SetText(text);
        entity.Add(name);
        entity.Add(new Transform { Position = position, Rotation = Quaternion.Identity, Scale = scale });
        if (sphere)
            Looks.AddSphere(entity, colour);
        else
            Looks.AddCube(entity, colour);
        entity.Add(new Tint { Base = Looks.Material(colour), Lit = Looks.Material(Playground.Lit(colour)) });
        return entity;
    }

    private Vector3 NextColour()
    {
        hue = (hue + 0.618034f) % 1.0f;
        return FromHsv(hue, 0.7f, 0.95f);
    }

    private static Vector3 FromHsv(float hue, float saturation, float value)
    {
        float sector = hue * 6;
        int index = (int)sector % 6;
        float fraction = sector - MathF.Floor(sector);
        float p = value * (1 - saturation);
        float q = value * (1 - saturation * fraction);
        float t = value * (1 - saturation * (1 - fraction));
        return index switch
        {
            0 => new Vector3(value, t, p),
            1 => new Vector3(q, value, p),
            2 => new Vector3(p, value, t),
            3 => new Vector3(p, q, value),
            4 => new Vector3(t, p, value),
            _ => new Vector3(value, p, q),
        };
    }
}

// Sweeps the paddle, once per fixed step, before physics steps. Physics moves a kinematic body to where its Transform
// says.
[System(Phase.Tick)]
public sealed class PaddleMover : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float step = context.Time.TickRate;
        foreach (var (entity, transform, paddle) in context.Query<Transform, Paddle>())
        {
            paddle.Time += step;
            transform.Position.X = -paddle.Reach * MathF.Cos(paddle.Time * paddle.Speed);
        }
    }
}

// Reads what physics reports: a body entering or leaving the trigger zone wears its paler material or its own again,
// and a sphere hitting something hard is logged. Each reader sees each event once. They are read every frame and acted
// on only in this scene, so the other scenes' triggers and contacts are neither logged here nor logged later.
[System(Phase.Update, Order = 110)]
public sealed class PlaygroundEvents : ISystem
{
    private readonly EventReader<TriggerEnter> enters = new();
    private readonly EventReader<TriggerExit> exits = new();
    private readonly EventReader<ContactBegin> contacts = new();

    public void OnUpdate(in SystemContext context)
    {
        bool active = Playground.IsActive(in context);
        foreach (var enter in enters.Read())
        {
            if (!active)
                continue;
            Entity body = enter.OtherBody;
            context.Log.Info($"TriggerEnter: {Playground.NameOf(body)} entered the {Playground.NameOf(enter.Trigger)}.");
            Retint(body, +1);
        }

        foreach (var exit in exits.Read())
        {
            if (!active)
                continue;
            Entity body = exit.OtherBody;
            context.Log.Info($"TriggerExit: {Playground.NameOf(body)} left the {Playground.NameOf(exit.Trigger)}.");
            Retint(body, -1);
        }

        foreach (var contact in contacts.Read())
        {
            // Only the hard hits: a resting pile begins small contacts all the time.
            if (!active || contact.RelativeSpeed < 3.0f)
                continue;
            context.Log.Info($"ContactBegin: {Playground.NameOf(contact.BodyA)} and " +
                             $"{Playground.NameOf(contact.BodyB)} met at {contact.RelativeSpeed:0.0} m/s, " +
                             $"at {Playground.Format(contact.Point)}.");
        }
    }

    // The body is in `change` more trigger zones; it wears its paler material while it is in any.
    private static void Retint(Entity body, int change)
    {
        if (!body.IsValid || !body.Has<Tint>() || !body.Has<MeshRenderer>())
            return;
        var tint = body.Get<Tint>();
        tint.Inside = Math.Max(0, tint.Inside + change);
        body.Get<MeshRenderer>().Value.Material = tint.Inside > 0 ? tint.Lit : tint.Base;
    }
}

// The scene's controls: J kicks every dynamic body, a left click in the scene casts a ray.
[System(Phase.Update, Order = 120)]
public sealed class PlaygroundInput : ISystem
{
    private readonly EventReader<DemoSceneClickEvent> clicks = new();
    private readonly Random random = new(7);

    public void OnUpdate(in SystemContext context)
    {
        // Read every frame, so a click in another scene is not acted on here later.
        bool active = Playground.IsActive(in context);

        if (active && Keyboard.JustPressed(Key.J))
            Kick(in context);

        foreach (var click in clicks.Read())
        {
            if (active)
                Cast(in context, click.Ray);
        }
    }

    // An upward impulse with a random sideways part, scaled by mass so every body jumps alike.
    private void Kick(in SystemContext context)
    {
        int kicked = 0;
        foreach (var (entity, body, tint) in context.Query<RigidBody, Tint>())
        {
            if (body.MotionType != MotionType.Dynamic)
                continue;
            var direction = new Vector3(Spread(), 6 + 2 * (float)random.NextDouble(), Spread());
            if (PhysicsWorld.AddImpulse(entity, direction * body.Mass))
                kicked++;
        }
        context.Log.Info($"J: kicked {kicked} bodies.");
    }

    private float Spread() => 3 * (float)random.NextDouble() - 1.5f;

    // The nearest body along the click's ray, logged; a dynamic one is pushed along the ray.
    private static void Cast(in SystemContext context, Ray ray)
    {
        if (!PhysicsWorld.Raycast(ray.Origin, ray.Direction, 200, out RaycastHit hit))
        {
            context.Log.Info("Raycast: the click hit no body.");
            return;
        }

        Entity body = hit.Body;
        context.Log.Info($"Raycast: hit {Playground.NameOf(body)} at {Playground.Format(hit.Point)}, " +
                         $"{hit.Distance:0.0} m away, normal {Playground.Format(hit.Normal)}.");
        if (body.Has<RigidBody>() && body.Get<RigidBody>().MotionType == MotionType.Dynamic)
            PhysicsWorld.AddImpulse(body, ray.Direction * 4);
    }
}
