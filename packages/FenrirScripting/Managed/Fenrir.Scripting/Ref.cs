namespace Fenrir.Scripting;

/// <summary>
/// A view of one component in the engine's storage: it points at the component's memory, so reading and writing
/// its fields reads and writes the real data. Every <see cref="IComponent"/> struct gets a generated property per
/// public field on this type, returning <c>ref</c> to it, so a write needs no <c>ref</c> at the call site.
/// </summary>
/// <remarks>
/// It is a <c>ref struct</c> on purpose: the pointer is only good until the engine next changes its storage, so a
/// <c>Ref&lt;T&gt;</c> cannot be kept in a field, captured by a lambda, used across an <c>await</c> or put in a collection.
/// The compiler enforces all of that.
/// </remarks>
public readonly unsafe ref struct Ref<T> where T : unmanaged, IComponent
{
    private readonly T* pointer;

    /// <summary>Wraps a pointer to a component. The engine hands these out; scripts do not build their own.</summary>
    public Ref(T* pointer) => this.pointer = pointer;

    /// <summary>Whether this points at nothing.</summary>
    public bool IsNull => pointer == null;

    /// <summary>The component itself, by reference. Reading it copies; writing a field of it writes the storage.</summary>
    public ref T Value
    {
        get
        {
            if (pointer == null)
                ThrowNull();
            return ref *pointer;
        }
    }

    private static void ThrowNull() => throw new InvalidOperationException($"This Ref<{typeof(T).Name}> does not point at a component.");
}
