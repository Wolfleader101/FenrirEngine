using Fenrir.Demo;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Renderer;
using Fenrir.Scripting;

namespace PhysicsDemo.Scripts;

// CHARACTER (C#): A CHARACTER SCENE MADE AND RUN ONLY FROM C#
//
// The scene after "Character (C++)" in PhysicsDemo's list. The scaffold gives its FenrirScene a CSharpScene resource
// naming it (src/Character.cpp, CharacterCSharpScene); everything in it is made here: a capsule character body
// (Fenrir.Physics's CharacterBody beside its Collider) on a small level of its own, the game code that walks it, and the
// follow camera.
//
// The controller is a Tick system, as a game's would be: it steers the horizontal Velocity physics wrote back towards
// the keys' walk by at most the acceleration, adds gravity from PhysicsWorld.Gravity, and jumps only from walkable
// ground. Physics moves the character after the Tick systems and writes Velocity back trimmed by what it touched, and
// State beside it, which the controller reads next tick.
//
// The level:
//   West    a 30° slide ramp up to a ledge 2 m high. StopOnSlope starts off, so the character slides down it.
//   East    an ice patch: its PhysicsMaterial friction, read from State.GroundCollider, lowers the acceleration, so the
//           character is slow to start and to stop on it.
//   South   a crouch tunnel 1.3 m high. Crouching writes the Collider's shape once; standing up inside waits, with
//           State.ShapeBlocked, until there is room.
//   South-west  a trigger pad, logging TriggerEnter and TriggerExit naming the character.
//   North-east  a kinematic lift, so State.CarriedVelocity shows after jumping off it.
//   North   a ghost pane in a layer the character's CollisionFilter masks out, which it walks through, and past it a
//           test wall, for the dash and the push out.
//
// The camera is a CharacterCamera system: it follows the character and writes the Transform of the scene's Camera entity,
// whose CharacterCameraRig holds where it looks. The character's look is an entity of its own, moved onto the character
// every frame, because scaling the character's Transform would scale its collider too.
//
//   WASD / arrows  walk, relative to the camera (only the arrows while the camera is free)
//   Shift          run
//   Space          jump
//   Q / E          turn the camera
//   C              free the camera (it stays where it is; F3 is the free-fly camera) and snap it back
//   X              toggle crouch
//   T              teleport to the start
//   1              toggle StopOnSlope
//   L              log the whole state
//   P              toggle the autopilot, which walks the ramp, the ice, the tunnel, the trigger pad and the lift
//   F              dash: MoveCast the character's shape 8 m ahead, filtered like the character with Layers, and
//                  teleport it the safe distance
//   K              push out: put the character half into the test wall, log Penetrations, and apply Depenetrate
//
// The character's name shows its readings every frame, shown as screen text: ground state and what it stands on, velocity, moved velocity,
// carried velocity while it has some, onWall, onCeiling and shapeBlocked while set, the ground's friction off the
// default, how many times it has landed, and how often its ground state changed in the last second. Each ground change
// is logged with its tick. As in the other C# scenes, the scene's state lives in components, so it survives a hot reload.

// The scene's own marker, on one entity: Built once the builder has made it.
public struct CharacterSceneMarker : IComponent
{
    public int Built;
}

// The follow camera's aim, on the scene's Camera entity: degrees about world +Y (-90 looks along -Z) and above the
// horizon, and whether it follows the character (the legacy DemoCamera's Yaw, Pitch and Active).
public struct CharacterCameraRig : IComponent
{
    public float Yaw;
    public float Pitch;
    public int Active;
}

// The player's character: what the keys and the autopilot ask of the controller, and what it remembers between ticks.
public struct CharacterPlayer : IComponent
{
    public int Held;       // The walk, run and camera keys held, one CharacterScene.Hold bit each.
    public bool Jump;      // Space, until the next tick.
    public bool Dash;      // F, until the next tick.
    public bool PushOut;   // K, until the next tick.
    public bool Crouched;  // Asked for; the shape in use differs while State.ShapeBlocked.
    public bool Autopilot;
    public int Waypoint;
    public float OnWaypoint; // Seconds walking to the waypoint.
    public float Waited;     // Seconds since arriving at it.
    public int Tick;
    public GroundState LastGround;
    public int Landings;
    public float Second;         // Seconds into the ground-change count's second.
    public int ChangesThisSecond;
    public int ChangesPerSecond; // In the last whole second.
}

// The character's look: a box the size of the shape in use, moved onto the character every frame.
public struct CharacterLook : IComponent
{
    public Entity Character;
}

// The lift: game code moves it, kinematic, by writing its Transform each tick.
public struct CharacterLift : IComponent
{
    public float Time; // Seconds into its cycle so far.
}

public static class CharacterScene
{
    // This scene's number in its CSharpScene, as C++ gives it (CharacterCSharpScene in src/PhysicsScenes.hpp).
    public const uint SceneId = 3;

    // CharacterPlayer.Held's bits, for the keys that act while held.
    [Flags]
    public enum Hold
    {
        None = 0,
        W = 1 << 0,
        A = 1 << 1,
        S = 1 << 2,
        D = 1 << 3,
        Up = 1 << 4,
        Left = 1 << 5,
        Down = 1 << 6,
        Right = 1 << 7,
        Shift = 1 << 8,
        Q = 1 << 9,
        E = 1 << 10,
    }

    // The keys held now, one Hold bit each.
    public static Hold Held()
    {
        (Key Key, Hold Bit)[] keys =
        [
            (Key.W, Hold.W), (Key.A, Hold.A), (Key.S, Hold.S), (Key.D, Hold.D), (Key.Up, Hold.Up),
            (Key.Left, Hold.Left), (Key.Down, Hold.Down), (Key.Right, Hold.Right), (Key.LeftShift, Hold.Shift),
            (Key.Q, Hold.Q), (Key.E, Hold.E),
        ];
        Hold held = Hold.None;
        foreach (var (key, bit) in keys)
        {
            if (Keyboard.IsDown(key))
                held |= bit;
        }
        return held;
    }

    // Collision layers, one bit each. The character is in its own layer, which the ghost pane's mask leaves out, so
    // the character walks through the pane and a query filtered like the character does not find it.
    public const uint CharacterLayer = 1u << 3;
    public const uint GhostLayer = 1u << 4;

    public const float FootToCentre = 0.8f;
    public const float WalkSpeed = 4;
    public const float RunSpeed = 8;
    public const float Acceleration = 30; // m/s², the most the walk changes the velocity by, at the default friction.
    public const float JumpSpeed = 5;
    public const float DefaultFriction = 0.5f; // PhysicsMaterial's, for ground with none.
    public const float IceFriction = 0.05f;
    public const float DashLength = 8;

    public static readonly CapsuleShape Standing = new() { HalfHeight = 0.5f, Radius = 0.3f };
    public static readonly CapsuleShape Crouching = new() { HalfHeight = 0.2f, Radius = 0.3f };

    public static readonly Vector3 Start = new(0, 0, 0);

    // The slide ramp, rising west to a ledge whose east face is at x = LedgeEastX.
    public const float LedgeTop = 2;
    public const float LedgeEastX = -7.5f;
    public static readonly float RampAngle = MathF.PI / 6;

    // The test wall's south face, which K puts the character half into.
    public const float TestWallZ = -8;

    // The lift, 2 m square, its top flush with the ground at the bottom: it waits 1.5 s, rises 3 m at 1.5 m/s, waits a
    // second and comes down again.
    public static readonly Vector3 LiftBottom = new(7, -0.1f, -6);
    public const float LiftCycle = 6.5f;

    public static float LiftHeight(float seconds)
    {
        float into = seconds % LiftCycle;
        if (into < 1.5f)
            return 0;
        if (into < 3.5f)
            return 1.5f * (into - 1.5f);
        if (into < 4.5f)
            return 3;
        return 3 - 1.5f * (into - 4.5f);
    }

    // Whether the active scene is this one, built.
    public static bool IsActive(in SystemContext context)
    {
        if (!CSharpScene.IsActive(SceneId, out _))
            return false;
        foreach (var (entity, marker) in context.Query<CharacterSceneMarker>())
            if (marker.Built != 0)
                return true;
        return false;
    }

    public static Quaternion AngleAxis(float angle, Vector3 unitAxis)
    {
        float sine = MathF.Sin(angle / 2);
        return new Quaternion(unitAxis.X * sine, unitAxis.Y * sine, unitAxis.Z * sine, MathF.Cos(angle / 2));
    }

    // The way the camera looks, on the ground plane: what "forward" walks.
    public static Vector3 Ahead(float yawDegrees)
    {
        float yaw = yawDegrees * MathF.PI / 180;
        return new Vector3(MathF.Cos(yaw), 0, MathF.Sin(yaw));
    }

    public static string Format(Vector3 value) => $"({value.X:0.00}, {value.Y:0.00}, {value.Z:0.00})";

    public static string NameOf(GroundState ground) => ground switch
    {
        GroundState.InAir => "in air",
        GroundState.OnGround => "on ground",
        GroundState.OnSteepGround => "on steep ground",
        _ => "touching",
    };

    // The character's Name holds its readings, so it is named for what it is.
    public static string NameOf(Entity entity)
    {
        if (!entity.IsValid)
            return "nothing";
        return entity.Has<CharacterPlayer>() ? "the character" : entity.Get<Name>().Value.GetText();
    }

    // The friction of what the character stands on: its collider's PhysicsMaterial, or the default.
    public static float GroundFriction(in CharacterState state)
    {
        if (state.GroundCollider == Entity.None)
            return DefaultFriction;
        Entity ground = state.GroundCollider;
        return ground.Has<PhysicsMaterial>() ? ground.Get<PhysicsMaterial>().Value.Friction : DefaultFriction;
    }

    // The shape physics uses: the one asked for, or the other while the change waits for room.
    public static CapsuleShape ShapeInUse(in CharacterPlayer player, in CharacterState state) =>
        player.Crouched != state.ShapeBlocked ? Crouching : Standing;

    // A query filtered as the character's own move is, by its CollisionFilter, missing the character itself.
    public static QueryFilter FilterLike(Entity character)
    {
        CollisionFilter filter = character.Has<CollisionFilter>() ? character.Get<CollisionFilter>().Value : new CollisionFilter();
        return new QueryFilter { Mask = filter.Mask, Layers = filter.Layers, IgnoreBody = character };
    }

    // A teleport: only the position is written, so physics puts it there with no sweep and finds its ground anew.
    public static void TeleportToStart(Entity character)
    {
        character.Get<Transform>().Value.Position = Start + new Vector3(0, FootToCentre + 0.5f, 0);
    }

    // An entity drawn as a cube in `colour`, `size` across. Its components land when the building system returns.
    public static Entity Box(string text, Vector3 position, Vector3 size, Vector3 colour, Quaternion? rotation = null)
    {
        Entity entity = Entity.Create();
        var name = new Name();
        name.SetText(text);
        entity.Add(name);
        entity.Add(new Transform { Position = position, Rotation = rotation ?? Quaternion.Identity, Scale = size });
        Looks.AddCube(entity, colour);
        return entity;
    }
}

// Builds the scene once each time it is entered.
[System(Phase.Update, Order = 300)]
public sealed class CharacterBuilder : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(CharacterScene.SceneId, out _))
            return;
        foreach (var (entity, marker) in context.Query<CharacterSceneMarker>())
            return; // Built already, this entry.
        Build(in context);
    }

    private static void Build(in SystemContext context)
    {
        Looks.Begin(in context, "Character (C#)");
        Entity made = Entity.Create();
        var sceneName = new Name();
        sceneName.SetText("Character (C#) scene");
        made.Add(sceneName);
        made.Add(new CharacterSceneMarker { Built = 1 });

        // Static, its top face at y = 0, with a white pad at the start.
        CharacterScene.Box("Ground", new Vector3(0, -0.5f, 0), new Vector3(40, 1, 40), new Vector3(0.45f, 0.45f, 0.45f))
            .Add(new Collider());
        CharacterScene.Box("Start", CharacterScene.Start + new Vector3(0, 0.005f, 0), new Vector3(1.2f, 0.01f, 1.2f),
            new Vector3(0.95f, 0.95f, 0.95f));

        MakeSlideRamp();
        MakeIce();
        MakeCrouchTunnel();
        MakeTriggerPad();
        MakeLift();
        MakeWalls();
        Entity character = MakeCharacter();

        // The scene's camera, which CharacterCamera moves, looking north.
        var label = new Name();
        label.SetText("Character camera");
        Entity rig = Entity.Create();
        rig.Add(label);
        rig.Add(new Transform
        {
            Position = CharacterScene.Start + new Vector3(0, 4, 8),
            Rotation = Looks.LookRotation(-90, -25),
            Scale = Vector3.One,
        });
        rig.Add(new Camera());
        rig.Add(new CharacterCameraRig { Yaw = -90, Pitch = -25, Active = 1 });

        // Its keys are in the scaffold's help (F1), from src/Character.cpp.
        context.Log.Info($"Character (C#): C# built a slide ramp, an ice patch, a crouch tunnel, a trigger pad, a lift " +
                         $"and a test wall, and a character body ({character}).");
    }

    // West: a slab 2 m wide and 0.4 m thick rising west at 30° from the ground to the ledge's top at its east face.
    private static void MakeSlideRamp()
    {
        var colour = new Vector3(0.55f, 0.5f, 0.4f);
        CharacterScene.Box("Ledge", new Vector3(CharacterScene.LedgeEastX - 2, CharacterScene.LedgeTop / 2, 0),
            new Vector3(4, CharacterScene.LedgeTop, 4), colour).Add(new Collider());

        float angle = CharacterScene.RampAngle;
        float length = CharacterScene.LedgeTop / MathF.Sin(angle);
        // Turned about Z so its east end is down; the top face's middle is halfway up, half its run east of the ledge.
        var middle = new Vector3(CharacterScene.LedgeEastX + 0.5f * length * MathF.Cos(angle), CharacterScene.LedgeTop / 2, 0);
        var up = new Vector3(MathF.Sin(angle), MathF.Cos(angle), 0); // The top face's normal.
        CharacterScene.Box("Slide ramp", middle - up * 0.2f, new Vector3(length, 0.4f, 2), new Vector3(0.5f, 0.55f, 0.45f),
            CharacterScene.AngleAxis(-angle, new Vector3(0, 0, 1))).Add(new Collider());
    }

    // East: a patch of ice 6 m by 3 m, a thin slab on the ground. Physics does not read its friction for the
    // character; the controller does, through State.GroundCollider.
    private static void MakeIce()
    {
        Entity ice = CharacterScene.Box("Ice", new Vector3(6, 0.01f, 0), new Vector3(6, 0.02f, 3),
            new Vector3(0.75f, 0.9f, 1.0f));
        ice.Add(new Collider());
        ice.Add(new PhysicsMaterial { Friction = CharacterScene.IceFriction });
    }

    // South: a tunnel 8 m long, 1.6 m wide and 1.3 m high, between a crouched head (1.0 m) and a standing one (1.6 m).
    private static void MakeCrouchTunnel()
    {
        var colour = new Vector3(0.35f, 0.5f, 0.65f);
        foreach (float x in new[] { -1.0f, 1.0f })
            CharacterScene.Box("Tunnel wall", new Vector3(x, 0.75f, 8), new Vector3(0.4f, 1.5f, 8), colour).Add(new Collider());
        // Its underside 1.3 m up, its top 1.5 m.
        CharacterScene.Box("Tunnel roof", new Vector3(0, 1.4f, 8), new Vector3(2.4f, 0.2f, 8), colour).Add(new Collider());
    }

    // South-west: a pad on the ground with a trigger reaching a metre above and below it (half extents are before
    // scale, so 100 times the pad's 0.01). Its mask takes only the character's layer, so the ground it sinks into is
    // not reported.
    private static void MakeTriggerPad()
    {
        Entity pad = CharacterScene.Box("Trigger pad", new Vector3(-5, 0.005f, 6), new Vector3(2, 0.01f, 2),
            new Vector3(0.3f, 0.75f, 0.35f));
        pad.Add(new Collider(new BoxShape { HalfExtents = new Vector3(0.5f, 100, 0.5f) }));
        pad.Add(new Trigger());
        pad.Add(new CollisionFilter { Mask = CharacterScene.CharacterLayer });
    }

    // North-east: a lift 2 m square, moved by CharacterLiftMover.
    private static void MakeLift()
    {
        Entity lift = CharacterScene.Box("Lift", CharacterScene.LiftBottom, new Vector3(2, 0.2f, 2),
            new Vector3(0.3f, 0.6f, 0.4f));
        lift.Add(new RigidBody { MotionType = MotionType.Kinematic });
        lift.Add(new Collider());
        lift.Add(new CharacterLift());
    }

    // North: a pane of the ghost layer, which the character's CollisionFilter masks out, and past it the test wall.
    private static void MakeWalls()
    {
        Entity pane = CharacterScene.Box("Ghost pane", new Vector3(0, 1.25f, -3), new Vector3(3, 2.5f, 0.1f),
            new Vector3(0.7f, 0.85f, 0.9f));
        pane.Add(new Collider());
        pane.Add(new CollisionFilter { Layers = CharacterScene.GhostLayer, Mask = ~CharacterScene.CharacterLayer });

        CharacterScene.Box("Test wall", new Vector3(0, 1.5f, CharacterScene.TestWallZ - 0.5f), new Vector3(4, 3, 1),
            new Vector3(0.55f, 0.4f, 0.3f)).Add(new Collider());
    }

    // A standing capsule half a metre above the start, and its look.
    private static Entity MakeCharacter()
    {
        Entity character = Entity.Create();
        var name = new Name();
        name.SetText("Character");
        character.Add(name);
        character.Add(new Transform
        {
            Position = CharacterScene.Start + new Vector3(0, CharacterScene.FootToCentre + 0.5f, 0),
            Rotation = Quaternion.Identity,
            Scale = Vector3.One,
        });
        character.Add(new Collider(CharacterScene.Standing));
        character.Add(new CharacterBody { StopOnSlope = false });
        character.Add(new CollisionFilter { Layers = CharacterScene.CharacterLayer });
        character.Add(new CharacterPlayer());

        Entity look = CharacterScene.Box("Character look", Vector3.Zero, new Vector3(0.6f, 1.6f, 0.6f),
            new Vector3(0.95f, 0.85f, 0.3f));
        look.Add(new CharacterLook { Character = character });
        return character;
    }
}

// The scene's keys. Presses act at once, or ask the next tick to; the held keys are kept for the controller and the
// camera.
[System(Phase.Update, Order = 310)]
public sealed class CharacterInput : ISystem
{
    // The keys a press of acts, in the order they are handled within a frame.
    private static readonly Key[] Presses =
        [Key.Space, Key.F, Key.K, Key.X, Key.T, Key.Num1, Key.L, Key.P, Key.C];

    public void OnUpdate(in SystemContext context)
    {
        if (!CharacterScene.IsActive(in context))
            return;
        foreach (Key key in Presses)
        {
            if (Keyboard.JustPressed(key))
                OnKey(in context, key);
        }

        CharacterScene.Hold held = CharacterScene.Held();
        foreach (var (entity, player) in context.Query<CharacterPlayer>())
            player.Held = (int)held;
    }

    private static void OnKey(in SystemContext context, Key key)
    {
        foreach (var (entity, player, body) in context.Query<CharacterPlayer, CharacterBody>())
        {
            switch (key)
            {
            case Key.Space:
                player.Jump = true;
                break;
            case Key.F:
                player.Dash = true;
                break;
            case Key.K:
                player.PushOut = true;
                break;
            case Key.X:
                CharacterCrouch.Set(in context, entity, ref player.Value, !player.Crouched);
                break;
            case Key.T:
                CharacterScene.TeleportToStart(entity);
                context.Log.Info("T: teleported to the start");
                break;
            case Key.Num1:
                body.StopOnSlope = !body.StopOnSlope;
                context.Log.Info(body.StopOnSlope ? "1: StopOnSlope on: stands still on the ramp"
                                                  : "1: StopOnSlope off: slides down the ramp");
                break;
            case Key.L:
                LogState(in context, entity, in body.Value, in player.Value);
                break;
            case Key.P:
                player.Autopilot = !player.Autopilot;
                player.Waypoint = 0;
                player.OnWaypoint = 0;
                player.Waited = 0;
                context.Log.Info(player.Autopilot ? "Autopilot: walking the route" : "Autopilot off");
                break;
            case Key.C:
                foreach (var (rig, camera) in context.Query<CharacterCameraRig>())
                {
                    camera.Active = camera.Active == 0 ? 1 : 0;
                    context.Log.Info(camera.Active != 0
                        ? "Camera follows the character"
                        : "Camera free: it stays where it is (F3 flies the free-fly camera), the arrows walk");
                }
                break;
            }
        }
    }

    private static void LogState(in SystemContext context, Entity character, in CharacterBody body,
                                 in CharacterPlayer player)
    {
        ref readonly CharacterState state = ref body.State;
        context.Log.Info($"Tick {player.Tick}: at {CharacterScene.Format(character.Get<Transform>().Value.Position)}, " +
                         $"{CharacterScene.NameOf(state.Ground)} on {CharacterScene.NameOf(state.GroundBody)} " +
                         $"({CharacterScene.NameOf(state.GroundCollider)}, at {CharacterScene.Format(state.GroundPoint)}, " +
                         $"normal {CharacterScene.Format(state.GroundNormal)}, moving at " +
                         $"{CharacterScene.Format(state.GroundVelocity)}, turning at " +
                         $"{CharacterScene.Format(state.GroundAngularVelocity)}), velocity {CharacterScene.Format(body.Velocity)}, " +
                         $"moved at {CharacterScene.Format(state.MovedVelocity)}, carried " +
                         $"{CharacterScene.Format(state.CarriedVelocity)}, onWall {state.OnWall}, onCeiling {state.OnCeiling}, " +
                         $"shapeBlocked {state.ShapeBlocked}");
    }
}

// Game code's crouch: the character's Collider written once. Physics stands it up once there is room.
public static class CharacterCrouch
{
    public static void Set(in SystemContext context, Entity character, ref CharacterPlayer player, bool crouched)
    {
        if (crouched == player.Crouched)
            return;
        player.Crouched = crouched;
        character.Get<Collider>().Value.Shape = crouched ? CharacterScene.Crouching : CharacterScene.Standing;
        context.Log.Info(crouched ? "Crouch" : "Stand up (once there is room)");
    }
}

// Moves the lift each tick, before physics steps: physics moves a kinematic body to where its Transform says.
[System(Phase.Tick)]
public sealed class CharacterLiftMover : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, transform, lift) in context.Query<Transform, CharacterLift>())
        {
            lift.Time += context.Time.TickRate;
            transform.Position = CharacterScene.LiftBottom + new Vector3(0, CharacterScene.LiftHeight(lift.Time), 0);
        }
    }
}

// The character's game code, once each tick before physics moves it: reads the last move's State, then steers.
[System(Phase.Tick, Order = 10)]
public sealed class CharacterController : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float dt = context.Time.TickRate;
        float yaw = -90;
        bool following = true;
        foreach (var (rig, camera) in context.Query<CharacterCameraRig>())
        {
            yaw = camera.Yaw;
            following = camera.Active != 0;
        }
        float liftTime = 0;
        foreach (var (entity, lift) in context.Query<CharacterLift>())
            liftTime = lift.Time;

        foreach (var (entity, playerRef, bodyRef) in context.Query<CharacterPlayer, CharacterBody>())
        {
            ref CharacterPlayer player = ref playerRef.Value;
            ref CharacterBody body = ref bodyRef.Value;
            // The scene's physics world is made after its first tick with bodies: gravity is read from the second on.
            if (++player.Tick == 1 || !body.Enabled)
                continue;
            NoteGroundChange(in context, ref player, body.State.Ground, dt);
            if (player.Dash)
                Dash(in context, entity, yaw);
            if (player.PushOut)
                PushOut(in context, entity);
            player.Dash = false;
            player.PushOut = false;

            // Steered from the velocity physics wrote back, never set outright, so what physics trimmed stays trimmed
            // and a slide keeps its speed. Ice lowers how fast the walk changes it. With no walk on a slope that
            // StopOnSlope does not hold, nothing brakes, so the character slides down at gravity's pace, not a creep.
            Vector3 walk = player.Autopilot
                ? CharacterAutopilot.Walk(in context, entity, ref player, liftTime, dt)
                : KeyboardWalk((CharacterScene.Hold)player.Held, yaw, following);
            bool sliding = walk == Vector3.Zero && !body.StopOnSlope && body.State.Ground == GroundState.OnGround &&
                           body.State.GroundNormal.Y < 0.999f;
            if (!sliding)
            {
                float grip = Math.Clamp(CharacterScene.GroundFriction(in body.State) / CharacterScene.DefaultFriction,
                    0.1f, 1);
                var horizontal = new Vector3(body.Velocity.X, 0, body.Velocity.Z);
                Vector3 change = walk - horizontal;
                float most = CharacterScene.Acceleration * grip * dt;
                float length = change.Length;
                horizontal += length > most ? change * (most / length) : change;
                body.Velocity.X = horizontal.X;
                body.Velocity.Z = horizontal.Z;
            }

            // Gravity is game code's: physics adds none.
            body.Velocity += PhysicsWorld.Gravity * dt;
            if (player.Jump && body.State.Ground == GroundState.OnGround)
                body.Velocity.Y = CharacterScene.JumpSpeed;
            player.Jump = false;
        }
    }

    // The keys' walk on the ground plane, relative to where the camera looks. While the camera is free, WASD is the
    // camera's, so only the arrows walk.
    private static Vector3 KeyboardWalk(CharacterScene.Hold held, float yaw, bool following)
    {
        bool Down(CharacterScene.Hold arrow, CharacterScene.Hold letter) =>
            (held & arrow) != 0 || (following && (held & letter) != 0);
        float forward = (Down(CharacterScene.Hold.Up, CharacterScene.Hold.W) ? 1 : 0) -
                        (Down(CharacterScene.Hold.Down, CharacterScene.Hold.S) ? 1 : 0);
        float right = (Down(CharacterScene.Hold.Right, CharacterScene.Hold.D) ? 1 : 0) -
                      (Down(CharacterScene.Hold.Left, CharacterScene.Hold.A) ? 1 : 0);
        if (forward == 0 && right == 0)
            return Vector3.Zero;
        Vector3 ahead = CharacterScene.Ahead(yaw);
        var side = new Vector3(-ahead.Z, 0, ahead.X);
        Vector3 direction = ahead * forward + side * right;
        float speed = (held & CharacterScene.Hold.Shift) != 0 ? CharacterScene.RunSpeed : CharacterScene.WalkSpeed;
        return direction / direction.Length * speed;
    }

    // Each ground change, logged with its tick and counted for the readings; a change to the ground is a landing.
    private static void NoteGroundChange(in SystemContext context, ref CharacterPlayer player, GroundState ground, float dt)
    {
        player.Second += dt;
        if (player.Second >= 1)
        {
            player.Second -= 1;
            player.ChangesPerSecond = player.ChangesThisSecond;
            player.ChangesThisSecond = 0;
        }
        if (ground == player.LastGround)
            return;
        if (ground == GroundState.OnGround)
            player.Landings++;
        player.ChangesThisSecond++;
        context.Log.Info($"Tick {player.Tick}: character {CharacterScene.NameOf(player.LastGround)} -> " +
                         $"{CharacterScene.NameOf(ground)}");
        player.LastGround = ground;
    }

    // F: the character's shape swept ahead, filtered as its own move is, and the character teleported as far as it
    // can go and stay clear. The ghost pane is in a layer the character's mask leaves out, so the dash passes it.
    private static void Dash(in SystemContext context, Entity character, float yaw)
    {
        Transform pose = character.Get<Transform>().Value;
        CollisionShape shape = character.Get<Collider>().Value.Shape;
        Vector3 ahead = CharacterScene.Ahead(yaw);
        float distance = CharacterScene.DashLength;
        if (PhysicsWorld.MoveCast(shape, pose, ahead * CharacterScene.DashLength, out MoveCastHit hit,
                                  CharacterScene.FilterLike(character)))
        {
            distance = hit.Distance;
            context.Log.Info($"F: dash blocked by {CharacterScene.NameOf(hit.Body)} after {hit.Distance:0.00} m of " +
                             $"{CharacterScene.DashLength:0} m, at {CharacterScene.Format(hit.Point)}");
        }
        else
        {
            context.Log.Info($"F: dashed the whole {CharacterScene.DashLength:0} m: nothing in the way");
        }
        character.Get<Transform>().Value.Position = pose.Position + ahead * distance;
    }

    // K: the character's pose half into the test wall (its centre on the wall's face), what overlaps it there, and
    // the character teleported to that pose moved out by Depenetrate.
    private static void PushOut(in SystemContext context, Entity character)
    {
        Transform pose = character.Get<Transform>().Value;
        pose.Position = new Vector3(0, CharacterScene.FootToCentre + 0.05f, CharacterScene.TestWallZ);
        CollisionShape shape = character.Get<Collider>().Value.Shape;
        QueryFilter filter = CharacterScene.FilterLike(character);
        Span<PenetrationHit> hits = stackalloc PenetrationHit[4];
        int count = PhysicsWorld.Penetrations(shape, pose, hits, filter);
        Vector3 offset = PhysicsWorld.Depenetrate(shape, pose, filter);
        string deepest = count > 0 ? $", deepest {hits[0].Depth:0.00} m into {CharacterScene.NameOf(hits[0].Body)}" : "";
        context.Log.Info($"K: half into the test wall at {CharacterScene.Format(pose.Position)}: {count} " +
                         $"{(count == 1 ? "penetration" : "penetrations")}{deepest}; Depenetrate moves it " +
                         $"{CharacterScene.Format(offset)}");
        character.Get<Transform>().Value.Position = pose.Position + offset;
        character.Get<CharacterBody>().Value.Velocity = Vector3.Zero;
    }
}

// P: walks the route by itself, a waypoint at a time, because a smoke run can only tap keys.
public static class CharacterAutopilot
{
    public enum Crouch
    {
        Keep,
        Down,
        Up,
    }

    public readonly record struct Waypoint(
        Vector3 At,
        bool Jump = false,      // Jump on arriving.
        bool BoardLift = false, // Wait on arriving until the lift begins its wait at the bottom.
        bool Ride = false,      // Once there, stand for the wait wherever it slides or the ground carries it.
        Crouch Crouch = Crouch.Keep,
        float Wait = 0,         // Seconds to stand once there.
        float GiveUp = 10);     // Seconds to try for it.

    // The slide ramp (standing halfway up, it slides down), onto the ice (stopping there, it slides on) and back, crouched into the tunnel and up
    // inside it (blocked until the exit), onto the trigger pad, and up on the lift, jumping off it as it rises.
    private static readonly Waypoint[] Route =
    [
        new(new Vector3(-3, 0, 0)),
        new(new Vector3(-5.8f, 0, 0), Ride: true, Wait: 2.5f, GiveUp: 4),
        new(new Vector3(2, 0, 0)),
        new(new Vector3(6, 0, 0), Ride: true, Wait: 2, GiveUp: 4),
        new(new Vector3(2, 0, 0), Wait: 0.5f, GiveUp: 6),
        new(new Vector3(0, 0, 2.5f), Crouch: Crouch.Down),
        new(new Vector3(0, 0, 6), Crouch: Crouch.Up),
        new(new Vector3(0, 0, 13.5f), Wait: 0.5f),
        new(new Vector3(-3, 0, 13.5f)),
        new(new Vector3(-5, 0, 6), Wait: 1),
        new(new Vector3(3, 0, -1)),
        new(new Vector3(7, 0, -3.5f), BoardLift: true, GiveUp: 10),
        new(new Vector3(7, 0, -6), Ride: true, Wait: 1.6f),
        new(new Vector3(7, 0, -6.7f), Jump: true),
        new(new Vector3(7, 0, -9.5f), Wait: 0.5f),
        new(CharacterScene.Start, Wait: 0.5f),
    ];

    // The walk towards the next waypoint, jumping, crouching and waiting where it says.
    public static Vector3 Walk(in SystemContext context, Entity character, ref CharacterPlayer player, float liftTime,
                               float dt)
    {
        Waypoint next = Route[player.Waypoint % Route.Length];
        player.OnWaypoint += dt;
        Vector3 to = next.At - character.Get<Transform>().Value.Position;
        to.Y = 0;
        bool there = to.Length < 0.3f || (next.Ride && player.Waited > 0);
        bool boardable = !next.BoardLift || liftTime % CharacterScene.LiftCycle < 0.5f;
        if (there && !boardable && player.OnWaypoint < next.GiveUp)
            return Vector3.Zero;
        if (!there && player.OnWaypoint < next.GiveUp)
        {
            player.Waited = 0;
            return to / to.Length * CharacterScene.WalkSpeed;
        }
        if (player.Waited == 0)
        {
            player.Jump = player.Jump || next.Jump;
            if (next.Crouch != Crouch.Keep)
                CharacterCrouch.Set(in context, character, ref player, next.Crouch == Crouch.Down);
        }
        player.Waited += dt;
        if (player.Waited >= next.Wait)
        {
            player.Waypoint++;
            player.Waited = 0;
            player.OnWaypoint = 0;
            if (player.Waypoint % Route.Length == 0)
            {
                CharacterScene.TeleportToStart(character);
                context.Log.Info("Autopilot: round the route; teleported to the start");
            }
        }
        return Vector3.Zero;
    }
}

// Logs the trigger pad's TriggerEnter and TriggerExit, naming the character. Read in every scene, so an event of
// another scene is not logged here later.
[System(Phase.Update, Order = 320)]
public sealed class CharacterTriggers : ISystem
{
    private readonly EventReader<TriggerEnter> enters = new();
    private readonly EventReader<TriggerExit> exits = new();

    public void OnUpdate(in SystemContext context)
    {
        bool active = CharacterScene.IsActive(in context);
        foreach (var enter in enters.Read())
        {
            if (active)
                context.Log.Info($"TriggerEnter: {CharacterScene.NameOf(enter.OtherBody)} ({enter.OtherBody}) " +
                                 $"entered the {CharacterScene.NameOf(enter.Trigger)}");
        }
        foreach (var exit in exits.Read())
        {
            if (active)
                context.Log.Info($"TriggerExit: {CharacterScene.NameOf(exit.OtherBody)} ({exit.OtherBody}) " +
                                 $"left the {CharacterScene.NameOf(exit.Trigger)}");
        }
    }
}

// The follow camera, as game code writes one: it orbits the character at a fixed distance and pitch, Q and E turn it,
// and it writes where it is into its Camera entity's Transform. Freed (Active 0), it leaves the camera where it is and
// keeps its yaw for the walk.
[System(Phase.Update, Order = 330)]
public sealed class CharacterCamera : ISystem
{
    private const float Distance = 9;
    private const float TurnSpeed = 90; // Degrees per second.

    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, transform, player) in context.Query<Transform, CharacterPlayer>())
        {
            var held = (CharacterScene.Hold)player.Held;
            Vector3 target = transform.Position + new Vector3(0, 0.6f, 0);
            foreach (var (rig, pose, camera) in context.Query<Transform, CharacterCameraRig>())
            {
                if (camera.Active == 0)
                    continue;
                float turn = ((held & CharacterScene.Hold.E) != 0 ? 1 : 0) - ((held & CharacterScene.Hold.Q) != 0 ? 1 : 0);
                camera.Yaw += turn * TurnSpeed * context.Time.DeltaTime;
                float yaw = camera.Yaw * MathF.PI / 180;
                float pitch = camera.Pitch * MathF.PI / 180;
                var front = new Vector3(MathF.Cos(pitch) * MathF.Cos(yaw), MathF.Sin(pitch), MathF.Cos(pitch) * MathF.Sin(yaw));
                pose.Position = target - front * Distance;
                pose.Rotation = Looks.LookRotation(camera.Yaw, camera.Pitch);
            }
        }
    }
}

// The character's look and its readings in its name, every frame, shown as screen text.
[System(Phase.Update, Order = 340)]
public sealed class CharacterReadings : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, transform, look) in context.Query<Transform, CharacterLook>())
        {
            Entity character = look.Character;
            CharacterPlayer player = character.Get<CharacterPlayer>().Value;
            CapsuleShape shape = CharacterScene.ShapeInUse(in player, in character.Get<CharacterBody>().Value.State);
            transform.Position = character.Get<Transform>().Value.Position;
            transform.Scale = new Vector3(0.6f, 2 * (shape.HalfHeight + shape.Radius), 0.6f);
        }

        var shown = new List<Entity>();
        foreach (var (entity, body, player) in context.Query<CharacterBody, CharacterPlayer>())
        {
            entity.Get<Name>().Value.SetText(Reading(in body.Value, in player.Value));
            shown.Add(entity);
        }
        Looks.ShowReadouts(shown);
    }

    private static string Reading(in CharacterBody body, in CharacterPlayer player)
    {
        ref readonly CharacterState state = ref body.State;
        string reading = CharacterScene.NameOf(state.Ground);
        if (state.GroundBody != Entity.None)
            reading += $": {CharacterScene.NameOf(state.GroundBody)}";
        reading += $", v {CharacterScene.Format(body.Velocity)}, moved {CharacterScene.Format(state.MovedVelocity)}";
        if (state.CarriedVelocity != Vector3.Zero)
            reading += $", carried {CharacterScene.Format(state.CarriedVelocity)}";
        reading += state.OnWall ? ", on wall" : "";
        reading += state.OnCeiling ? ", on ceiling" : "";
        reading += state.ShapeBlocked ? ", shape blocked" : "";
        float friction = CharacterScene.GroundFriction(in state);
        reading += friction != CharacterScene.DefaultFriction ? $", friction {friction:0.00}" : "";
        reading += body.Enabled ? "" : ", disabled";
        return reading + $", {player.Landings} landings, {player.ChangesPerSecond} ground changes/s";
    }
}
