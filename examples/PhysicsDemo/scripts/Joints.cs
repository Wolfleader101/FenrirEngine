using Fenrir.Demo;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Scripting;

namespace PhysicsDemo.Scripts;

// JOINTS (C#): A JOINTS SCENE MADE AND RUN ONLY FROM C#
//
// The scene after "Joints (C++)" in PhysicsDemo's list. The scaffold gives its FenrirScene a CSharpScene resource naming
// it (src/Joints.cpp, JointsCSharpScene); everything in it is made here, from Fenrir.Physics's Joint and its kinds. It
// shows the four kinds the C++ scene does not:
//
//   A bridge of four planks hinged end to end between two static posts, sagging. Its middle hinge breaks over 250 N:
//     the JointBroken is read and logged.
//   The bridge's middle joint swaps between a hinge and a fixed joint by assigning its Kind: fixed, the two middle
//     planks are one beam and the bridge is stiffer.
//   A lift: a platform on a slider up a static column, with limits, its motor driven through the Kind.Slider view.
//   A spring rope: a distance joint with a soft spring, holding a weight that bobs from a point fixed to the world.
//   A limb: an arm hanging from a static shoulder on a swing-twist joint with cone limits and an orientation motor.
//
//   B   drops a 10 kg weight on the bridge, made at runtime with Entity.Create.
//   R   repairs every broken joint, by writing Enabled = true; one still over its break force breaks again.
//   K   swaps the bridge's middle joint between hinge and fixed.
//   1   cycles the lift's motor: off, up, down, to a position half way.
//   2   moves the limb's motor target between its rest pose and a raised pose.
//   0   logs every joint's state.
//
// Each joint entity's name shows its joint state every frame (PhysicsWorld.GetJointState), shown as screen text: the
// bridge's angles and loads, the lift's offset, the rope's length, the limb's swing. As in Playground.cs, every system
// acts only while the scene is active and keeps the scene's state in components, so it survives a hot reload.

// The scene's own marker, on one entity: how far the build has gone (JointsScene.BodiesMade, JointsScene.Built).
public struct JointsSceneMarker : IComponent
{
    public int Built;
}

// Which joint of the scene a joint entity is, for its label and its anchor.
public enum JointPart : int
{
    Bridge,
    Lift,
    Rope,
    Limb,
}

public struct JointRole : IComponent
{
    public JointPart Part;
    public int Index; // Along the bridge, from the left post: 0 to 4.
}

// A weight B dropped, numbered from 1.
public struct BridgeWeight : IComponent
{
    public int Number;
}

public static class JointsScene
{
    // This scene's number in its CSharpScene, as C++ gives it (JointsCSharpScene in src/PhysicsScenes.hpp).
    public const uint SceneId = 2;

    // JointsSceneMarker.Built as the build goes: the bodies made, waiting a frame for their Transforms; then built.
    public const int BodiesMade = 1;
    public const int Built = 2;

    public const int BridgeMiddle = 2;
    public const float BreakForce = 250;

    // The bridge: four planks of PlankLength, sagging between posts whose hinges are at the same height. Each plank's
    // angle below level is its equilibrium for four equal planks: the outer ones' tangent three times the inner ones'.
    public const int Planks = 4;
    public const float PlankLength = 1.25f;
    public static readonly float InnerSlope = 10 * MathF.PI / 180;
    public static readonly float OuterSlope = MathF.Atan(3 * MathF.Tan(InnerSlope));
    public static readonly Vector3 BridgeLeft =
        new(-3.5f - PlankLength * (MathF.Cos(OuterSlope) + MathF.Cos(InnerSlope)), -1.0f, -12);

    public static readonly Vector3 LiftStart = new(1.9f, -3.3f, -12);
    public const float LiftTravel = 2.8f;
    public static readonly Vector3 Hook = new(3.75f, 1.5f, -12);
    public static readonly Vector3 RopeWeightStart = new(3.75f, 0.1f, -12);
    public const float RopeWeightRadius = 0.25f;
    public const float RopeLength = 1.6f;
    public static readonly Vector3 Shoulder = new(5.75f, 0.3f, -12);
    public const float ArmLength = 1.6f;
    public const float LimbCone = 1.0f;
    public const float LimbRaised = 0.9f;

    // Frames whose X axis, the axis a hinge turns about, a slider slides along and a swing-twist joint twists about,
    // points along world Z, up and down.
    public static readonly Quaternion XAlongZ = AngleAxis(-MathF.PI / 2, new Vector3(0, 1, 0));
    public static readonly Quaternion XUp = AngleAxis(MathF.PI / 2, new Vector3(0, 0, 1));
    public static readonly Quaternion XDown = AngleAxis(-MathF.PI / 2, new Vector3(0, 0, 1));

    // The limb's raised pose: a swing about its frame's Z, which is world Z, lifting the arm towards +X.
    public static readonly Quaternion Raised = AngleAxis(LimbRaised, new Vector3(0, 0, 1));

    // Whether the active scene is this one, built.
    public static bool IsActive(in SystemContext context)
    {
        if (!CSharpScene.IsActive(SceneId, out _))
            return false;
        foreach (var (entity, marker) in context.Query<JointsSceneMarker>())
            if (marker.Built == Built)
                return true;
        return false;
    }

    public static Quaternion AngleAxis(float angle, Vector3 unitAxis)
    {
        float sine = MathF.Sin(angle / 2);
        return new Quaternion(unitAxis.X * sine, unitAxis.Y * sine, unitAxis.Z * sine, MathF.Cos(angle / 2));
    }

    // Plank `plank`'s angle below level: down to the middle, then up.
    public static float SlopeOf(int plank) => plank switch
    {
        0 => OuterSlope,
        1 => InnerSlope,
        2 => -InnerSlope,
        _ => -OuterSlope,
    };

    // Bridge hinge `index`, from the left post's (0) to the right post's (Planks).
    public static Vector3 BridgeHinge(int index)
    {
        Vector3 point = BridgeLeft;
        for (int plank = 0; plank < index; plank++)
            point += new Vector3(MathF.Cos(SlopeOf(plank)), -MathF.Sin(SlopeOf(plank)), 0) * PlankLength;
        return point;
    }

    // Where a joint's rest pose is, in world space. For the rope, the weight's top: the hook is its world end.
    public static JointFrame AnchorOf(JointRole role) => role.Part switch
    {
        JointPart.Bridge => new JointFrame { Position = BridgeHinge(role.Index), Rotation = XAlongZ },
        JointPart.Lift => new JointFrame { Position = LiftStart, Rotation = XUp },
        JointPart.Rope => new JointFrame { Position = RopeWeightStart + new Vector3(0, RopeWeightRadius, 0) },
        _ => new JointFrame { Position = Shoulder, Rotation = XDown },
    };

    public static string LabelOf(JointRole role) => role.Part switch
    {
        JointPart.Bridge when role.Index == BridgeMiddle => "Bridge middle",
        JointPart.Bridge => $"Bridge hinge {role.Index}",
        JointPart.Lift => "Lift slider",
        JointPart.Rope => "Spring rope",
        _ => "Limb",
    };

    public static string Format(Vector3 value) => $"({value.X:0.00}, {value.Y:0.00}, {value.Z:0.00})";

    // How far a rotation turns, in radians.
    public static float AngleOf(Quaternion rotation) => 2 * MathF.Acos(MathF.Min(1, MathF.Abs(rotation.W)));

    // An entity drawn as `kind` in `colour`.
    public static Entity Make(string text, Vector3 position, Quaternion rotation, Vector3 scale, MeshKind kind,
                              Vector3 colour)
    {
        Entity entity = Entity.Create();
        var name = new Name();
        name.SetText(text);
        entity.Add(name);
        entity.Add(new Transform { Position = position, Rotation = rotation, Scale = scale });
        Looks.Add(entity, kind, colour);
        return entity;
    }
}

// Builds the scene once each time it is entered, in two frames. The first makes the bodies and the joint entities,
// each joint linked to bodies made a moment before in the same call. FrameFromWorld reads where the bodies are, which
// is only known once their Transforms land, when this system returns; so the joints start disabled and the moving bodies
// kinematic, held where they were made. The next frame gives each joint its frames from FrameFromWorld, enables it and
// lets the bodies move.
[System(Phase.Update, Order = 200)]
public sealed class JointsBuilder : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(JointsScene.SceneId, out _))
            return;
        foreach (var (entity, marker) in context.Query<JointsSceneMarker>())
        {
            if (marker.Built == JointsScene.BodiesMade)
            {
                marker.Built = JointsScene.Built;
                Join(in context);
            }
            return;
        }

        Looks.Begin(in context, "Joints (C#)");
        Entity made = Entity.Create();
        var label = new Name();
        label.SetText("Joints (C#) scene");
        made.Add(label);
        made.Add(new JointsSceneMarker { Built = JointsScene.BodiesMade });
        MakeBodies(in context);
    }

    private static void MakeBodies(in SystemContext context)
    {
        // Static, its top face at y = -3.75.
        Entity ground = JointsScene.Make("Ground", new Vector3(0, -4, -12), Quaternion.Identity, new Vector3(16, 0.5f, 16),
            MeshKind.Cube, new Vector3(0.45f, 0.45f, 0.45f));
        ground.Add(new Collider());

        MakeBridge();
        MakeLift();
        MakeRope();
        MakeLimb();
    }

    // Two static posts, their tops at the hinges, and four planks between them, hinged to each other and to the posts.
    private static void MakeBridge()
    {
        var wood = new Vector3(0.55f, 0.35f, 0.2f);
        Entity Post(string text, Vector3 hinge, float outwards)
        {
            float height = hinge.Y + 3.75f + 0.1f;
            Entity post = JointsScene.Make(text, new Vector3(hinge.X + outwards * 0.25f, hinge.Y + 0.1f - height / 2, -12),
                Quaternion.Identity, new Vector3(0.5f, height, 1), MeshKind.Cube, wood * 0.7f);
            post.Add(new Collider());
            return post;
        }

        Entity left = Post("Bridge post (left)", JointsScene.BridgeHinge(0), -1);
        Entity right = Post("Bridge post (right)", JointsScene.BridgeHinge(JointsScene.Planks), 1);

        Entity above = left;
        for (int plank = 0; plank <= JointsScene.Planks; plank++)
        {
            Entity next = right;
            if (plank < JointsScene.Planks)
            {
                Vector3 centre = (JointsScene.BridgeHinge(plank) + JointsScene.BridgeHinge(plank + 1)) / 2;
                next = JointsScene.Make($"Plank {plank}", centre,
                    JointsScene.AngleAxis(-JointsScene.SlopeOf(plank), new Vector3(0, 0, 1)),
                    new Vector3(JointsScene.PlankLength - 0.05f, 0.12f, 0.9f), MeshKind.Cube,
                    plank % 2 == 0 ? wood : wood * 1.25f);
                next.Add(new RigidBody { MotionType = MotionType.Kinematic, Mass = 2 });
                next.Add(new Collider());
            }

            var hinge = new Joint { BodyA = above, BodyB = next, Kind = new HingeJoint(), Enabled = false };
            if (plank == JointsScene.BridgeMiddle)
                hinge.BreakForce = JointsScene.BreakForce;
            MakeJoint(hinge, new JointRole { Part = JointPart.Bridge, Index = plank });
            above = next;
        }
    }

    // A static column and a platform beside it, which slides up it from where it starts (its lowest) to LiftTravel above.
    private static void MakeLift()
    {
        Vector3 start = JointsScene.LiftStart;
        float top = start.Y + JointsScene.LiftTravel + 0.5f;
        Entity column = JointsScene.Make("Lift column", new Vector3(start.X - 0.75f, (top - 3.75f) / 2, -12),
            Quaternion.Identity, new Vector3(0.3f, top + 3.75f, 0.3f), MeshKind.Cube, new Vector3(0.6f, 0.6f, 0.65f));
        column.Add(new Collider());
        Entity platform = JointsScene.Make("Lift platform", start, Quaternion.Identity, new Vector3(1.2f, 0.15f, 1.0f),
            MeshKind.Cube, new Vector3(0.2f, 0.5f, 0.85f));
        platform.Add(new RigidBody { MotionType = MotionType.Kinematic, Mass = 5 });
        platform.Add(new Collider());

        var slider = new SliderJoint
        {
            Limits = new JointLimits { Enabled = true, Min = 0, Max = JointsScene.LiftTravel },
        };
        MakeJoint(new Joint { BodyA = column, BodyB = platform, Kind = slider, Enabled = false },
            new JointRole { Part = JointPart.Lift });
    }

    // A weight on a soft rope from a hook fixed to the world. It starts nearer the hook than the rope's length, so it
    // drops, stretches the spring and bobs.
    private static void MakeRope()
    {
        JointsScene.Make("Hook (a world point, no body)", JointsScene.Hook, Quaternion.Identity,
            new Vector3(0.12f, 0.12f, 0.12f), MeshKind.Cube, new Vector3(0.9f, 0.9f, 0.9f));
        float diameter = 2 * JointsScene.RopeWeightRadius;
        Entity weight = JointsScene.Make("Rope weight", JointsScene.RopeWeightStart, Quaternion.Identity,
            new Vector3(diameter, diameter, diameter), MeshKind.Sphere, new Vector3(0.9f, 0.3f, 0.6f));
        weight.Add(new RigidBody { MotionType = MotionType.Kinematic, Mass = 3, LinearDamping = 0 });
        weight.Add(new Collider(new SphereShape()));

        var rope = new DistanceJoint
        {
            MinDistance = JointsScene.RopeLength,
            MaxDistance = JointsScene.RopeLength,
            Spring = new JointSpring { Frequency = 1, Damping = 0.03f },
        };
        MakeJoint(new Joint { BodyA = weight, BodyB = Entity.None, Kind = rope, Enabled = false },
            new JointRole { Part = JointPart.Rope });
    }

    // An arm hanging straight down from a static shoulder: its rest pose. The motor holds it there, gently.
    private static void MakeLimb()
    {
        Vector3 shoulder = JointsScene.Shoulder;
        Entity block = JointsScene.Make("Shoulder", shoulder + new Vector3(0, 0.2f, 0), Quaternion.Identity,
            new Vector3(0.6f, 0.4f, 0.6f), MeshKind.Cube, new Vector3(0.6f, 0.6f, 0.65f));
        block.Add(new Collider());
        Entity arm = JointsScene.Make("Arm", shoulder - new Vector3(0, JointsScene.ArmLength / 2, 0), Quaternion.Identity,
            new Vector3(0.25f, JointsScene.ArmLength, 0.25f), MeshKind.Cube, new Vector3(0.85f, 0.6f, 0.45f));
        arm.Add(new RigidBody { MotionType = MotionType.Kinematic, Mass = 2 });
        arm.Add(new Collider());

        var limb = new SwingTwistJoint
        {
            SwingYLimit = JointsScene.LimbCone,
            SwingZLimit = JointsScene.LimbCone,
            Motor = new SwingTwistMotor
            {
                Mode = JointMotorMode.Position,
                Spring = new JointSpring { Frequency = 1, Damping = 1 },
            },
        };
        MakeJoint(new Joint { BodyA = block, BodyB = arm, Kind = limb, Enabled = false },
            new JointRole { Part = JointPart.Limb });
    }

    private static void MakeJoint(Joint joint, JointRole role)
    {
        Entity entity = Entity.Create();
        var name = new Name();
        name.SetText(JointsScene.LabelOf(role));
        entity.Add(name);
        entity.Add(joint);
        entity.Add(role);
    }

    // The bodies are where they were made, so each joint's frames put its rest pose there.
    private static void Join(in SystemContext context)
    {
        foreach (var (entity, body) in context.Query<RigidBody>())
        {
            if (body.MotionType == MotionType.Kinematic)
                body.MotionType = MotionType.Dynamic;
        }

        int joints = 0;
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            ref Joint value = ref joint.Value;
            JointFrame anchor = JointsScene.AnchorOf(role.Value);
            Entity bodyA = value.BodyA;
            if (value.BodyB == Entity.None)
            {
                // The world overload: FrameB is the anchor itself, in world space. A rope's world end is the hook, so
                // its frames are apart by the distance the weight starts from the hook.
                JointFrames frames = Joint.FrameFromWorld(bodyA, anchor);
                value.FrameA = frames.FrameA;
                value.FrameB = new JointFrame { Position = JointsScene.Hook };
            }
            else
            {
                JointFrames frames = Joint.FrameFromWorld(bodyA, value.BodyB, anchor);
                value.FrameA = frames.FrameA;
                value.FrameB = frames.FrameB;
            }
            value.Enabled = true;
            joints++;

            // A swing forwards, about world X (the limb frame's Y), which its cone stops.
            if (role.Value.Part == JointPart.Limb)
                value.BodyB.Get<RigidBody>().Value.AngularVelocity = new Vector3(8, 0, 0);
        }

        // Its keys are in the scaffold's help (F1), from src/Joints.cpp.
        context.Log.Info($"Joints (C#): C# built a bridge, a lift, a spring rope and a limb, with {joints} joints.");
    }
}

// Logs each joint that breaks. Read every frame, so a break in another scene is not logged here later.
[System(Phase.Update, Order = 210)]
public sealed class JointsBreaks : ISystem
{
    private readonly EventReader<JointBroken> breaks = new();

    public void OnUpdate(in SystemContext context)
    {
        bool active = JointsScene.IsActive(in context);
        foreach (var broken in breaks.Read())
        {
            Entity joint = broken.Joint;
            if (!active || !joint.Has<JointRole>())
                continue;
            context.Log.Info($"JointBroken: {JointsScene.LabelOf(joint.Get<JointRole>().Value)} broke at " +
                             $"{broken.Force.Length:0.0} N, {broken.Torque.Length:0.0} N m. R repairs it.");
        }
    }
}

// The scene's keys.
[System(Phase.Update, Order = 220)]
public sealed class JointsInput : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!JointsScene.IsActive(in context))
            return;
        if (Keyboard.JustPressed(Key.B))
            DropWeight(in context);
        if (Keyboard.JustPressed(Key.R))
            Repair(in context);
        if (Keyboard.JustPressed(Key.K))
            SwapMiddle(in context);
        if (Keyboard.JustPressed(Key.Num1))
            CycleLift(in context);
        if (Keyboard.JustPressed(Key.Num2))
            ToggleLimb(in context);
        if (Keyboard.JustPressed(Key.Num0))
            LogStates(in context);
    }

    // B: a weight above the bridge's middle, made now; it lands when this system's changes do.
    private static void DropWeight(in SystemContext context)
    {
        int number = 1;
        foreach (var (entity, weight) in context.Query<BridgeWeight>())
            number = Math.Max(number, weight.Number + 1);
        float middle = JointsScene.BridgeHinge(JointsScene.BridgeMiddle).X;
        Entity box = JointsScene.Make($"Weight {number}", new Vector3(middle, 1.5f, -12), Quaternion.Identity,
            new Vector3(0.5f, 0.5f, 0.5f), MeshKind.Cube, new Vector3(0.25f, 0.25f, 0.3f));
        box.Add(new RigidBody { Mass = 10 });
        box.Add(new Collider());
        box.Add(new BridgeWeight { Number = number });
        context.Log.Info($"B: dropped weight {number} (10 kg) on the bridge.");
    }

    // R: every disabled joint enabled again, in place.
    private static void Repair(in SystemContext context)
    {
        int repaired = 0;
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            if (joint.Value.Enabled)
                continue;
            joint.Value.Enabled = true;
            repaired++;
        }
        context.Log.Info($"R: repaired {repaired} {(repaired == 1 ? "joint" : "joints")}.");
    }

    // K: the middle joint's kind assigned, keeping its frames, its break force and whether it is enabled.
    private static void SwapMiddle(in SystemContext context)
    {
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            if (role.Value.Part != JointPart.Bridge || role.Value.Index != JointsScene.BridgeMiddle)
                continue;
            ref Joint value = ref joint.Value;
            bool toFixed = value.Kind.Type == JointType.Hinge;
            value.Kind = toFixed ? new FixedJoint() : new HingeJoint();
            context.Log.Info(toFixed ? "K: the bridge's middle joint is fixed: the middle planks are one beam."
                                     : "K: the bridge's middle joint is a hinge again.");
        }
    }

    // 1: the lift's next motor setting, edited in place through the slider's view.
    private static void CycleLift(in SystemContext context)
    {
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            if (role.Value.Part != JointPart.Lift)
                continue;
            ref JointMotor motor = ref joint.Value.Kind.Slider.Motor;
            string what;
            if (motor.Mode == JointMotorMode.Off)
            {
                motor.Mode = JointMotorMode.Velocity;
                motor.TargetVelocity = 1;
                what = "up at 1 m/s, to its top limit";
            }
            else if (motor.Mode == JointMotorMode.Velocity && motor.TargetVelocity > 0)
            {
                motor.TargetVelocity = -1;
                what = "down at 1 m/s, to its bottom limit";
            }
            else if (motor.Mode == JointMotorMode.Velocity)
            {
                motor.Mode = JointMotorMode.Position;
                motor.TargetPosition = JointsScene.LiftTravel / 2;
                what = $"to {JointsScene.LiftTravel / 2:0.0} m, half way";
            }
            else
            {
                motor.Mode = JointMotorMode.Off;
                what = "off: the platform drops to its bottom limit";
            }
            context.Log.Info($"1: lift motor {what}.");
        }
    }

    // 2: the limb's motor target, its rest pose or raised, edited in place through the swing-twist view.
    private static void ToggleLimb(in SystemContext context)
    {
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            if (role.Value.Part != JointPart.Limb)
                continue;
            ref SwingTwistMotor motor = ref joint.Value.Kind.SwingTwist.Motor;
            bool raise = motor.TargetRotation == Quaternion.Identity;
            motor.TargetRotation = raise ? JointsScene.Raised : Quaternion.Identity;
            context.Log.Info(raise
                ? $"2: limb raised {JointsScene.LimbRaised:0.0} rad, inside its {JointsScene.LimbCone:0.0} rad cone."
                : "2: limb back to its rest pose.");
        }
    }

    // 0: every joint's full state.
    private static void LogStates(in SystemContext context)
    {
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            JointState state = PhysicsWorld.GetJointState(entity);
            Quaternion turn = state.Relative.Rotation;
            context.Log.Info($"{JointsScene.LabelOf(role.Value)} ({joint.Value.Kind.Type}" +
                             $"{(joint.Value.Enabled ? "" : ", off")}): force {JointsScene.Format(state.Force)} N, " +
                             $"torque {JointsScene.Format(state.Torque)} N m, frameB at " +
                             $"{JointsScene.Format(state.Relative.Position)} turned (w {turn.W:0.000}, x {turn.X:0.000}, " +
                             $"y {turn.Y:0.000}, z {turn.Z:0.000}), position {state.Position:0.000}");
        }
    }
}

// Each joint's readings in its name, every frame, shown as screen text: an angle or an offset, and how hard it pulls;
// "off" once disabled.
[System(Phase.Update, Order = 230)]
public sealed class JointsReadings : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!JointsScene.IsActive(in context))
            return;
        var shown = new List<Entity>();
        foreach (var (entity, joint, role) in context.Query<Joint, JointRole>())
        {
            string label = JointsScene.LabelOf(role.Value);
            entity.Get<Name>().Value.SetText(joint.Value.Enabled
                ? $"{label}: {Reading(in joint.Value, PhysicsWorld.GetJointState(entity))}"
                : $"{label}: off");
            shown.Add(entity);
        }
        Looks.ShowReadouts(shown);
    }

    private static string Reading(in Joint joint, JointState state)
    {
        float force = state.Force.Length;
        return joint.Kind.Type switch
        {
            JointType.Hinge => $"{state.Position:0.00} rad, {force:0} N",
            JointType.Slider => $"{state.Position:0.00} m, {force:0} N",
            JointType.Distance => $"{state.Position:0.00} m, {force:0} N",
            JointType.SwingTwist => $"swung {JointsScene.AngleOf(state.Relative.Rotation):0.00} rad, {force:0} N",
            _ => $"fixed, {force:0} N",
        };
    }
}
