using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// Reads the physics events every frame, as game code would, and keeps what it read for the tests.
[System(Phase.Update)]
public sealed class PhysicsEventRecorder : ISystem
{
    private readonly EventReader<ContactBegin> begins = new();
    private readonly EventReader<ContactEnd> ends = new();
    private readonly EventReader<TriggerEnter> enters = new();
    private readonly EventReader<TriggerExit> exits = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var begin in begins.Read())
            Events.Begins.Add(begin);
        foreach (var end in ends.Read())
            Events.Ends.Add(end);
        foreach (var enter in enters.Read())
            Events.Enters.Add(enter);
        foreach (var exit in exits.Read())
            Events.Exits.Add(exit);
    }
}

// What the recorder read, field by field, for the tests to call through Iris.
public static class Events
{
    public static readonly List<ContactBegin> Begins = new();
    public static readonly List<ContactEnd> Ends = new();
    public static readonly List<TriggerEnter> Enters = new();
    public static readonly List<TriggerExit> Exits = new();

    public static int ContactBeginCount() => Begins.Count;

    public static uint BeginBodyA(int index) => Begins[index].BodyA.Target.Value;

    public static uint BeginBodyB(int index) => Begins[index].BodyB.Target.Value;

    public static uint BeginColliderA(int index) => Begins[index].ColliderA.Target.Value;

    public static uint BeginColliderB(int index) => Begins[index].ColliderB.Target.Value;

    public static float BeginPoint(int index, int axis) => Axis(Begins[index].Point, axis);

    public static float BeginNormal(int index, int axis) => Axis(Begins[index].Normal, axis);

    public static float BeginRelativeSpeed(int index) => Begins[index].RelativeSpeed;

    public static int ContactEndCount() => Ends.Count;

    public static uint EndBodyA(int index) => Ends[index].BodyA.Target.Value;

    public static uint EndBodyB(int index) => Ends[index].BodyB.Target.Value;

    public static uint EndColliderA(int index) => Ends[index].ColliderA.Target.Value;

    public static uint EndColliderB(int index) => Ends[index].ColliderB.Target.Value;

    public static int TriggerEnterCount() => Enters.Count;

    public static uint EnterTrigger(int index) => Enters[index].Trigger.Target.Value;

    public static uint EnterOtherBody(int index) => Enters[index].OtherBody.Target.Value;

    public static uint EnterOtherCollider(int index) => Enters[index].OtherCollider.Target.Value;

    public static int TriggerExitCount() => Exits.Count;

    public static uint ExitTrigger(int index) => Exits[index].Trigger.Target.Value;

    public static uint ExitOtherBody(int index) => Exits[index].OtherBody.Target.Value;

    public static uint ExitOtherCollider(int index) => Exits[index].OtherCollider.Target.Value;

    // Switches reporting on the entity, in place, as a script would.
    public static void SetContactEvents(uint entity, bool enabled) =>
        new Entity(entity).Get<ContactEvents>().Value.Enabled = enabled;

    public static void SetTrigger(uint entity, bool enabled) => new Entity(entity).Get<Trigger>().Value.Enabled = enabled;

    private static float Axis(Vector3 value, int axis) => axis switch
    {
        0 => value.X,
        1 => value.Y,
        _ => value.Z,
    };
}
