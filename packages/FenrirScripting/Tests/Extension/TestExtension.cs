using Fenrir.Scripting;

namespace Fenrir.TestExtension;

// The native functions of the test extension, declared once: the generator writes each body and the field the host binds
// to the C++ function the test's extension declares as "Fenrir.TestExtension.Gadgets.<Method>".
public static partial class Gadgets
{
    [NativeFunction]
    public static partial int Add(int left, int right);

    [NativeFunction]
    public static partial Entity EchoEntity(Entity entity);

    // The entity's Transform height, as native code reads it; -1 if it has none.
    [NativeFunction]
    public static partial float HeightOf(Entity entity);

    [NativeFunction]
    public static partial void Lift(Entity entity, float by);

    // The UTF-8 byte count of the text, times ten, plus the number; the native side keeps the text it received.
    [NativeFunction]
    public static partial int Label(string text, int number);

    // Adds to the running total the extension keeps in its extension state, and returns the new total.
    [NativeFunction]
    public static partial int Tally(int by);
}

/// <summary>A shared struct of the test extension: plain data its natives would take by pointer.</summary>
public struct Reading
{
    public int Count;
    public float Weight;
}

/// <summary>A component the test extension gives C#: the native <c>Charge</c>.</summary>
[NativeComponent("Fenrir.TestExtension.Charge")]
public struct Charge : IComponent
{
    public int Level;
}

/// <summary>An event the test extension shares with C#: the native <c>Ping</c>.</summary>
[NativeEvent("Fenrir.TestExtension.Ping")]
public struct Ping : IEvent
{
    public int Value;
}
