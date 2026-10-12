using System.Reflection;
using System.Text;

namespace Fenrir.Scripting;

// The registry id of a native component type, looked up by the name in its [NativeComponent] attribute and remembered
// for as long as the registry it came from is the current one.
internal static unsafe class ComponentIds<T> where T : unmanaged, IReadOnlyComponent
{
    /// <summary>The type has no [NativeComponent] attribute, so the engine has nothing registered for it.</summary>
    internal const int NotNative = -2;

    /// <summary>The attribute names a component the host has not registered.</summary>
    internal const int NotRegistered = -1;

    /// <summary>The attribute names a component the host registered for a different C# type.</summary>
    internal const int WrongType = -3;

    private static uint generation;
    private static bool resolved;
    private static int id;

    internal static string? NativeName { get; } = typeof(T).GetCustomAttribute<NativeComponentAttribute>()?.Name;

    internal static int Id
    {
        get
        {
            uint current = Native.RegistryGeneration();
            if (!resolved || generation != current)
            {
                id = Resolve();
                generation = current;
                resolved = true;
            }
            return id;
        }
    }

    private static int Resolve()
    {
        if (NativeName == null)
            return NotNative;
        string typeName = typeof(T).FullName ?? typeof(T).Name;
        Span<byte> name = stackalloc byte[Encoding.UTF8.GetMaxByteCount(NativeName.Length)];
        Span<byte> type = stackalloc byte[Encoding.UTF8.GetMaxByteCount(typeName.Length)];
        int nameLength = Encoding.UTF8.GetBytes(NativeName, name);
        int typeLength = Encoding.UTF8.GetBytes(typeName, type);
        fixed (byte* nameBytes = name)
        fixed (byte* typeBytes = type)
            return Native.ComponentIdOf(nameBytes, nameLength, typeBytes, typeLength);
    }
}
