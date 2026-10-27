using System.Reflection;

namespace Fenrir.Scripting;

// What differs between the kinds of thing the registry names (components, events, resources): the attribute that names
// a native one, the native that resolves it, and the words of the exceptions. Everything else about looking an id up
// and remembering it is RegistryIds.
internal interface IRegistryKind
{
    /// <summary>The kind as the messages say it: "component", "event", "resource".</summary>
    static abstract string Noun { get; }

    /// <summary>Whether a type with no attribute can still be registered, under its own full name, because a script defines it.</summary>
    static abstract bool ScriptDefinable { get; }

    /// <summary>The name in the kind's attribute on the type, or null when it carries none.</summary>
    static abstract string? NativeName(Type type);

    /// <summary>The native that gives the id, or a <see cref="RegistryError"/> value.</summary>
    static abstract int Resolve(string name, string typeName);

    /// <summary>The exception message when nothing is registered under the name: with the attribute, or without one.</summary>
    static abstract string NotRegisteredMessage(string name, Type type, bool hasAttribute);
}

// The registry id of a type of one kind, looked up by the name in its attribute (or its full name, for a type a script
// defines) and remembered for as long as the registry it came from is the current one.
internal static unsafe class RegistryIds<T, TKind> where T : unmanaged where TKind : struct, IRegistryKind
{
    private static uint generation;
    private static bool resolved;
    private static int id;

    /// <summary>The name in the kind's attribute, or null for a type that carries none.</summary>
    internal static string? NativeName { get; } = TKind.NativeName(typeof(T));

    /// <summary>Whether the type carries the kind's attribute.</summary>
    internal static bool HasAttribute => NativeName != null;

    /// <summary>The name the engine knows the type by: the attribute's, or the type's own full name.</summary>
    internal static string Name { get; } = NativeName ?? typeof(T).FullName ?? typeof(T).Name;

    /// <summary>The id, or a <see cref="RegistryError"/> value, re-resolved whenever the registry's generation changes.</summary>
    internal static int Id
    {
        get
        {
            if (!HasAttribute && !TKind.ScriptDefinable)
                return (int)RegistryError.NotRegistered;
            uint current = Native.RegistryGeneration();
            if (!resolved || generation != current)
            {
                id = TKind.Resolve(Name, typeof(T).FullName ?? typeof(T).Name);
                generation = current;
                resolved = true;
            }
            return id;
        }
    }

    /// <summary>The id, or a clear exception saying why there is none.</summary>
    /// <exception cref="InvalidOperationException">
    /// The name is registered for a different C# type, or nothing is registered under it.
    /// </exception>
    internal static int Require()
    {
        int found = Id;
        if (found == (int)RegistryError.WrongType)
            throw new InvalidOperationException($"'{Name}' is registered for a different C# type than {typeof(T).FullName}; a native {TKind.Noun} is reached only through the type the host validated.");
        if (found == (int)RegistryError.NotRegistered)
            throw new InvalidOperationException(TKind.NotRegisteredMessage(Name, typeof(T), HasAttribute));
        return found;
    }
}

internal readonly struct ComponentKind : IRegistryKind
{
    public static string Noun => "component";

    public static bool ScriptDefinable => true;

    public static string? NativeName(Type type) => type.GetCustomAttribute<NativeComponentAttribute>()?.Name;

    public static unsafe int Resolve(string name, string typeName) => Native.ComponentIdOf(name, typeName);

    public static string NotRegisteredMessage(string name, Type type, bool hasAttribute) => hasAttribute
        ? $"The engine has no component registered as '{name}' (for {type.Name})."
        : $"The engine does not know {type.Name}: a component a script defines is registered when its assembly loads, and only if the script project uses the Fenrir scripting SDK.";
}

internal readonly struct EventKind : IRegistryKind
{
    public static string Noun => "event";

    public static bool ScriptDefinable => true;

    public static string? NativeName(Type type) => type.GetCustomAttribute<NativeEventAttribute>()?.Name;

    public static unsafe int Resolve(string name, string typeName) => Native.EventIdOf(name, typeName);

    public static string NotRegisteredMessage(string name, Type type, bool hasAttribute) => hasAttribute
        ? $"The engine has no event registered as '{name}' (for {type.Name})."
        : $"The engine does not know {type.Name}: an event a script defines is registered when its assembly loads, and only if the script project uses the Fenrir scripting SDK.";
}

internal readonly struct ResourceKind : IRegistryKind
{
    public static string Noun => "resource";

    // A resource is mirrored from one a scripting extension registers: a script cannot define one.
    public static bool ScriptDefinable => false;

    public static string? NativeName(Type type) => type.GetCustomAttribute<NativeResourceAttribute>()?.Name;

    public static unsafe int Resolve(string name, string typeName) => Native.ResourceIdOf(name, typeName);

    public static string NotRegisteredMessage(string name, Type type, bool hasAttribute) => hasAttribute
        ? $"The engine has no resource registered as '{name}' (for {type.Name}): is its package's scripting extension in ScriptingConfig::extensions?"
        : $"{type.Name} has no [NativeResource] attribute: a resource is mirrored from one a scripting extension registers.";
}
