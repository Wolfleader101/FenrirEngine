// Iris assigns every function pointer field below when the host shares this assembly.
#pragma warning disable CS0649

using Fenrir.Scripting;

namespace Fenrir.TestExtension;

// The native functions of the test extension. Each field is named by the binding-name rule for the native function
// "Fenrir.TestExtension.Gadgets.<Method>", so Iris binds it to the function the test's extension declares under that name.
// (Ticket 03 replaces these hand-written fields with [NativeFunction] declarations.)
public static unsafe class Gadgets
{
    public static delegate* unmanaged[Cdecl]<int, int, int> __native_Add;
    public static delegate* unmanaged[Cdecl]<uint, uint> __native_EchoEntity;
    public static delegate* unmanaged[Cdecl]<uint, float> __native_HeightOf;
    public static delegate* unmanaged[Cdecl]<uint, float, void> __native_Lift;
    // A native std::string_view parameter: UTF-8 bytes, then their length.
    public static delegate* unmanaged[Cdecl]<byte*, int, int, int> __native_Label;

    public static int Add(int left, int right) => __native_Add(left, right);

    public static Entity EchoEntity(Entity entity) => new(__native_EchoEntity(entity.Value));

    // The entity's Transform height, as native code reads it; -1 if it has none.
    public static float HeightOf(Entity entity) => __native_HeightOf(entity.Value);

    public static void Lift(Entity entity, float by) => __native_Lift(entity.Value, by);

    // The UTF-8 byte count of the text, times ten, plus the number; the native side keeps the text it received.
    public static int Label(string text, int number)
    {
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(text);
        fixed (byte* data = bytes) return __native_Label(data, bytes.Length, number);
    }
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
