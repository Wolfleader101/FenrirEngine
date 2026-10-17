using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// Joints from a script. The tests call these through Iris, then play the changes back as the runner does after a system.
public static class Joints
{
    // Gives the entity a `new Joint()`, with `new` of the kind numbered `kind` (as JointType numbers them) assigned, or
    // the joint's own new kind when `kind` is negative.
    public static void AddNewJoint(uint entity, int kind)
    {
        var joint = new Joint();
        if (kind >= 0)
        {
            joint.Kind = (JointType)kind switch
            {
                JointType.Fixed => new FixedJoint(),
                JointType.Ball => new BallJoint(),
                JointType.Distance => new DistanceJoint(),
                JointType.Hinge => new HingeJoint(),
                JointType.Slider => new SliderJoint(),
                _ => new SwingTwistJoint(),
            };
        }
        new Entity(entity).Add(joint);
    }

    public static void AddDefaultJoint(uint entity) => new Entity(entity).Add(default(Joint));

    // As a system builds a door: a static post at (x, y, z), a dynamic door 1.5 along X from it, and a joint entity
    // hinging them about X at the gap between them, all made and linked in this one call. Returns the door.
    public static uint BuildHingedDoor(float x, float y, float z)
    {
        var post = Entity.Create();
        post.Add(Placed(x, y, z));
        post.Add(new Collider());
        var door = Entity.Create();
        door.Add(Placed(x + 1.5f, y, z));
        door.Add(new RigidBody());
        door.Add(new Collider());
        var hinge = Entity.Create();
        hinge.Add(new Joint
        {
            BodyA = post,
            BodyB = door,
            FrameA = new JointFrame { Position = new Vector3(0.75f, 0, 0) },
            FrameB = new JointFrame { Position = new Vector3(-0.75f, 0, 0) },
            Kind = new HingeJoint(),
        });
        return door.Value;
    }

    private static Transform Placed(float x, float y, float z) =>
        new() { Position = new Vector3(x, y, z), Rotation = Quaternion.Identity, Scale = new Vector3(1, 1, 1) };

    // Makes the joint a hinge driven at `velocity` radians per second, by assigning a new kind.
    public static void MakeMotorHinge(uint joint, float velocity)
    {
        new Entity(joint).Get<Joint>().Value.Kind =
            new HingeJoint { Motor = new JointMotor { Mode = JointMotorMode.Velocity, TargetVelocity = velocity } };
    }

    // Edits the hinge's motor target in place, through its view.
    public static void SetHingeTarget(uint joint, float velocity)
    {
        new Entity(joint).Get<Joint>().Value.Kind.Hinge.Motor.TargetVelocity = velocity;
    }

    // Reads the joint's slider view, which must throw for any other kind: the exception's message, or null if it did not.
    public static string? SliderViewMessage(uint joint)
    {
        try
        {
            _ = new Entity(joint).Get<Joint>().Value.Kind.Slider.Friction;
            return null;
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }

    // The frames FrameFromWorld last gave, kept for the tests to read value by value.
    private static JointFrames lastFrames;

    public static void FrameFromWorld(uint bodyA, uint bodyB, float x, float y, float z, float w, float qx, float qy,
                                      float qz)
    {
        lastFrames = Joint.FrameFromWorld(new Entity(bodyA), new Entity(bodyB), Anchor(x, y, z, w, qx, qy, qz));
    }

    public static void FrameFromWorldToWorld(uint bodyA, float x, float y, float z, float w, float qx, float qy, float qz)
    {
        lastFrames = Joint.FrameFromWorld(new Entity(bodyA), Anchor(x, y, z, w, qx, qy, qz));
    }

    private static JointFrame Anchor(float x, float y, float z, float w, float qx, float qy, float qz) =>
        new() { Position = new Vector3(x, y, z), Rotation = new Quaternion(qx, qy, qz, w) };

    // One value of the last frames: frame 0 is FrameA, 1 is FrameB; values 0 to 2 are the position's X, Y and Z, and 3
    // to 6 the rotation's W, X, Y and Z.
    public static float LastFrame(int frame, int value)
    {
        JointFrame chosen = frame == 0 ? lastFrames.FrameA : lastFrames.FrameB;
        return value switch
        {
            0 => chosen.Position.X,
            1 => chosen.Position.Y,
            2 => chosen.Position.Z,
            3 => chosen.Rotation.W,
            4 => chosen.Rotation.X,
            5 => chosen.Rotation.Y,
            _ => chosen.Rotation.Z,
        };
    }
}
