namespace Fenrir.Scripting;

/// <summary>
/// A read-only view of one component in the engine's storage. For a component scripts must not change (an
/// <see cref="IReadOnlyComponent"/>) it is the only view there is; for any other component it is the cautious one.
/// Like <see cref="Ref{T}"/> it is a <c>ref struct</c>, good only for the call it was handed to, and the generator adds a
/// <c>ref readonly</c> property per field.
/// </summary>
public readonly unsafe ref struct ReadOnlyRef<T> where T : unmanaged, IReadOnlyComponent
{
    private readonly T* pointer;

    /// <summary>Wraps a pointer to a component. The engine hands these out; scripts do not build their own.</summary>
    public ReadOnlyRef(T* pointer) => this.pointer = pointer;

    /// <summary>Whether this points at nothing.</summary>
    public bool IsNull => pointer == null;

    /// <summary>The component itself, by readonly reference.</summary>
    public ref readonly T Value
    {
        get
        {
            if (pointer == null)
                ThrowNull();
            return ref *pointer;
        }
    }

    private static void ThrowNull() => throw new InvalidOperationException($"This ReadOnlyRef<{typeof(T).Name}> does not point at a component.");
}
