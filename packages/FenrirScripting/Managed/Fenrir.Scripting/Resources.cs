using System.Reflection;
using System.Text;

namespace Fenrir.Scripting;

/// <summary>
/// A native resource scripts may read but not change: a scene or app resource an extension registered with a read-only
/// <c>Make...Resource</c>. It is reached through <see cref="Resources.GetReadOnly{T}"/> as a <see cref="ReadOnlyRef{T}"/>,
/// which cannot be written through, and has no <see cref="Ref{T}"/>. The struct carries <see cref="NativeResourceAttribute"/>.
/// </summary>
/// <remarks>
/// It extends <see cref="IReadOnlyComponent"/> only so that <see cref="ReadOnlyRef{T}"/> and the generated field accessors
/// serve resources too. A resource is not a component: <see cref="Entity"/>'s component calls refuse it.
/// </remarks>
public interface IReadOnlyResource : IReadOnlyComponent
{
}

/// <summary>
/// Marks a struct as a native resource: plain data the engine keeps once per scene or once per app, mirrored from C++ and
/// reached through <see cref="Resources.Get{T}"/> as a <see cref="Ref{T}"/>, so writing its fields writes the engine's
/// storage. The struct carries <see cref="NativeResourceAttribute"/>. A resource with behaviour (an input device, debug
/// drawing) is not mirrored: its package gives C# a static class of native functions instead.
/// </summary>
public interface IResource : IComponent, IReadOnlyResource
{
}

/// <summary>
/// The engine's native resources, by type: a scene resource is the active scene's, an app resource the app's. Every call
/// works from any C# code, not only from systems.
/// </summary>
/// <remarks>
/// A <see cref="Ref{T}"/> to a resource is good for the call it was taken in, as one to a component is. Inserting and
/// removing a resource are structural changes: they are recorded, and play back with the rest when the system returns, so
/// a <see cref="Ref{T}"/> taken before stays valid and a read sees the state as it is now.
/// </remarks>
public static unsafe class Resources
{
    /// <summary>
    /// A view of the resource <typeparamref name="T"/> in the engine's storage, to read and write. When there is none now
    /// it logs a warning and returns an empty <see cref="Ref{T}"/> (<see cref="Ref{T}.IsNull"/>); use
    /// <see cref="TryGet{T}"/> for a resource that may be absent.
    /// </summary>
    /// <exception cref="InvalidOperationException">The engine has no resource registered for this type.</exception>
    public static Ref<T> Get<T>() where T : unmanaged, IResource => new((T*)PointerOrWarn<T>());

    /// <summary>A read-only view of the resource <typeparamref name="T"/>; otherwise as <see cref="Get{T}"/>.</summary>
    /// <exception cref="InvalidOperationException">The engine has no resource registered for this type.</exception>
    public static ReadOnlyRef<T> GetReadOnly<T>() where T : unmanaged, IReadOnlyResource => new((T*)PointerOrWarn<T>());

    /// <summary>Whether there is a resource <typeparamref name="T"/> now; if so, <paramref name="resource"/> views it.</summary>
    /// <exception cref="InvalidOperationException">The engine has no resource registered for this type.</exception>
    public static bool TryGet<T>(out Ref<T> resource) where T : unmanaged, IResource
    {
        resource = new((T*)Native.ResourceGet(IdOf<T>()));
        return !resource.IsNull;
    }

    /// <summary>A read-only <see cref="TryGet{T}"/>.</summary>
    /// <exception cref="InvalidOperationException">The engine has no resource registered for this type.</exception>
    public static bool TryGetReadOnly<T>(out ReadOnlyRef<T> resource) where T : unmanaged, IReadOnlyResource
    {
        resource = new((T*)Native.ResourceGet(IdOf<T>()));
        return !resource.IsNull;
    }

    /// <summary>
    /// Gives the active scene (for a scene resource) or the app a default <typeparamref name="T"/>, as C++ constructs it,
    /// when the system returns, replacing the one there is.
    /// </summary>
    /// <exception cref="InvalidOperationException">The type is not a writable resource the engine has registered.</exception>
    public static void Insert<T>() where T : unmanaged, IResource => Check<T>(Native.ResourceInsert(IdOf<T>(), null), "insert");

    /// <summary>
    /// Makes <paramref name="value"/> the resource <typeparamref name="T"/> when the system returns, replacing the one there is.
    /// </summary>
    /// <exception cref="InvalidOperationException">As for <see cref="Insert{T}()"/>.</exception>
    public static void Insert<T>(T value) where T : unmanaged, IResource =>
        Check<T>(Native.ResourceInsert(IdOf<T>(), &value), "insert");

    /// <summary>Removes the resource <typeparamref name="T"/> when the system returns. Removing one there is not does nothing.</summary>
    /// <exception cref="InvalidOperationException">As for <see cref="Insert{T}()"/>.</exception>
    public static void Remove<T>() where T : unmanaged, IResource => Check<T>(Native.ResourceRemove(IdOf<T>()), "remove");

    private static void* PointerOrWarn<T>() where T : unmanaged, IReadOnlyResource
    {
        void* pointer = Native.ResourceGet(IdOf<T>());
        if (pointer == null)
            Log.Warn($"Resources.Get: there is no {ResourceIds<T>.Name} resource now, so the view is empty; use TryGet for a resource that may be absent.");
        return pointer;
    }

    private static void Check<T>(int code, string what) where T : unmanaged, IReadOnlyResource
    {
        if (code == 0)
            return;
        throw new InvalidOperationException(code switch
        {
            -5 => $"Cannot {what} {typeof(T).Name}: scripting is not running.",
            -7 => $"Cannot {what} {typeof(T).Name}: the engine has no such resource.",
            -8 => $"Cannot {what} {typeof(T).Name}: scripts may only read it.",
            _ => $"Cannot {what} {typeof(T).Name}: the engine refused (code {code}).",
        });
    }

    // The registry id of a native resource type, or a clear exception.
    private static int IdOf<T>() where T : unmanaged, IReadOnlyResource
    {
        int id = ResourceIds<T>.Id;
        if (id == ResourceIds<T>.WrongType)
            throw new InvalidOperationException($"'{ResourceIds<T>.Name}' is registered for a different C# type than {typeof(T).FullName}; a native resource is reached only through the type the host validated.");
        if (id == ResourceIds<T>.NotRegistered)
            throw new InvalidOperationException(ResourceIds<T>.HasAttribute
                ? $"The engine has no resource registered as '{ResourceIds<T>.Name}' (for {typeof(T).Name}): is its package's scripting extension in ScriptingConfig::extensions?"
                : $"{typeof(T).Name} has no [NativeResource] attribute: a resource is mirrored from one a scripting extension registers.");
        return id;
    }
}

// The registry id of a native resource type, looked up by the name in its [NativeResource] attribute and remembered for as
// long as the registry it came from is the current one.
internal static unsafe class ResourceIds<T> where T : unmanaged, IReadOnlyResource
{
    internal const int NotRegistered = -1;
    internal const int WrongType = -3;

    private static uint generation;
    private static bool resolved;
    private static int id;

    /// <summary>Whether the type carries a [NativeResource] attribute.</summary>
    internal static bool HasAttribute { get; } = typeof(T).GetCustomAttribute<NativeResourceAttribute>() != null;

    /// <summary>The name in the [NativeResource] attribute, or the type's full name when it has none.</summary>
    internal static string Name { get; } =
        typeof(T).GetCustomAttribute<NativeResourceAttribute>()?.Name ?? typeof(T).FullName ?? typeof(T).Name;

    internal static int Id
    {
        get
        {
            if (!HasAttribute)
                return NotRegistered;
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
        string typeName = typeof(T).FullName ?? typeof(T).Name;
        Span<byte> name = stackalloc byte[Encoding.UTF8.GetMaxByteCount(Name.Length)];
        Span<byte> type = stackalloc byte[Encoding.UTF8.GetMaxByteCount(typeName.Length)];
        int nameLength = Encoding.UTF8.GetBytes(Name, name);
        int typeLength = Encoding.UTF8.GetBytes(typeName, type);
        fixed (byte* nameBytes = name)
        fixed (byte* typeBytes = type)
            return Native.ResourceIdOf(nameBytes, nameLength, typeBytes, typeLength);
    }
}
