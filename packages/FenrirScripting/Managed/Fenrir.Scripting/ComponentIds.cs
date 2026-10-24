using System.Reflection;

namespace Fenrir.Scripting;

// The registry id of a native component type, looked up by the name in its [NativeComponent] attribute and remembered
// for as long as the registry it came from is the current one.
internal static unsafe class ComponentIds<T> where T : unmanaged, IReadOnlyComponent
{
    /// <summary>The attribute names a component the host has not registered.</summary>
    internal const int NotRegistered = -1;

    /// <summary>The attribute names a component the host registered for a different C# type.</summary>
    internal const int WrongType = -3;

    private static uint generation;
    private static bool resolved;
    private static int id;

    /// <summary>Whether the type is a native resource (an <see cref="IReadOnlyResource"/>), which is never a component.</summary>
    internal static bool IsResource { get; } = typeof(IReadOnlyResource).IsAssignableFrom(typeof(T));

    /// <summary>The name in a [NativeComponent] attribute, or null for a component the script defines.</summary>
    internal static string? NativeName { get; } = typeof(T).GetCustomAttribute<NativeComponentAttribute>()?.Name;

    /// <summary>The name the engine knows the component by: the attribute's, or the type's own full name for a script component.</summary>
    internal static string Name { get; } = NativeName ?? typeof(T).FullName ?? typeof(T).Name;

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

    private static int Resolve() => Native.ComponentIdOf(Name, typeof(T).FullName ?? typeof(T).Name);
}
