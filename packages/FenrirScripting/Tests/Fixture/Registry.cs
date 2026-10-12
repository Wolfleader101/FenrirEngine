using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// A component the test target registers from outside FenrirScripting, under "Test.Health".
[NativeComponent("Test.Health")]
public struct Health : IComponent
{
    public int Current;
    public int Max;
}

// Names a component nobody registered.
[NativeComponent("Test.Nothing")]
public struct Unregistered : IComponent
{
    public int Value2;
}

// Claims the hierarchy's name for a struct of its own, to try to get a writable view of it.
[NativeComponent("Fenrir.Relationship")]
public struct FakeRelationship : IComponent
{
    public int Value2;
}

// A component with no [NativeComponent] attribute: not a native one.
public struct Plain : IComponent
{
    public int Value2;
}

// What scripts do with the native components, called by the tests through Iris.
public static class Registry
{
    public static float ReadPositionX(uint entity) => new Entity(entity).Get<Transform>().Position.X;

    public static float ReadRotationW(uint entity) => new Entity(entity).Get<Transform>().Rotation.W;

    public static float ReadScaleZ(uint entity) => new Entity(entity).Get<Transform>().Scale.Z;

    // Writes through the engine's own storage: the nested vector, the whole-struct assignment and the quaternion.
    public static void Move(uint entity)
    {
        var t = new Entity(entity).Get<Transform>();
        t.Position.X += 1;
        t.Position.Y = 2;
        t.Rotation.W = 0.5f;
        t.Scale = new Vector3(3, 3, 3);
    }

    public static bool HasTransform(uint entity) => new Entity(entity).Has<Transform>();

    public static bool HasHealth(uint entity) => new Entity(entity).Has<Health>();

    public static string ReadName(uint entity) => new Entity(entity).Get<Name>().Text;

    public static void WriteName(uint entity, string text)
    {
        var name = new Entity(entity).Get<Name>();
        name.Text = text;
    }

    // The hierarchy, read-only: the entity value of a link, or uint.MaxValue when there is none.
    public static uint ParentOf(uint entity) => Link(new Entity(entity).GetReadOnly<Relationship>().Parent);

    public static uint FirstChildOf(uint entity) => Link(new Entity(entity).GetReadOnly<Relationship>().FirstChild);

    public static uint NextSiblingOf(uint entity) => Link(new Entity(entity).GetReadOnly<Relationship>().NextSibling);

    private static uint Link(in EntityLink link) => link.IsNull ? uint.MaxValue : link.Target.Value;

    public static int HealthSum(uint entity)
    {
        var health = new Entity(entity).Get<Health>();
        health.Current += 5;
        return health.Current + health.Max;
    }

    // The exception text a script sees for each way a lookup can fail.
    public static string? MissingComponent(uint entity) => Message(() => new Entity(entity).Get<Transform>().Scale.X);

    public static string? NotRegistered(uint entity) => Message(() => new Entity(entity).Get<Unregistered>().Value2);

    public static string? FakeHierarchy(uint entity) => Message(() => new Entity(entity).Get<FakeRelationship>().Value2);

    // Looks up Health, which may or may not be registered yet.
    public static string? HealthLookup(uint entity) => Message(() => new Entity(entity).Get<Health>().Current);

    public static string? NotNative(uint entity) => Message(() => new Entity(entity).Get<Plain>().Value2);

    private static string? Message(Func<object> action)
    {
        try
        {
            action();
            return null;
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }
}
