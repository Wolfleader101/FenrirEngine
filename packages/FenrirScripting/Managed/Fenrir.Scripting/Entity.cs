namespace Fenrir.Scripting;

/// <summary>
/// An entity in the active scene: the full EnTT value, version bits included, so a stale handle to a destroyed
/// entity is invalid even after its slot is reused.
/// </summary>
public readonly struct Entity : IEquatable<Entity>
{
    public Entity(uint value) => Value = value;

    /// <summary>The raw EnTT entity value.</summary>
    public uint Value { get; }

    /// <summary>Whether this entity is alive in the active scene.</summary>
    public unsafe bool IsValid => Native.EntityIsValid(Value);

    /// <summary>Whether this entity is alive and has a component of type <typeparamref name="T"/>.</summary>
    /// <exception cref="InvalidOperationException">The type is not a component the engine has registered.</exception>
    public unsafe bool Has<T>() where T : unmanaged, IReadOnlyComponent => Native.ComponentHas(IdOf<T>(), Value);

    /// <summary>
    /// A view of this entity's <typeparamref name="T"/> in the engine's storage, to read and write.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    /// The entity is not alive, or does not have the component, or the type is not a component the engine has registered.
    /// </exception>
    public unsafe Ref<T> Get<T>() where T : unmanaged, IComponent => new((T*)PointerTo<T>());

    /// <summary>
    /// A read-only view of this entity's <typeparamref name="T"/>. This is the only view of a component scripts may not
    /// change, such as <see cref="Relationship"/>.
    /// </summary>
    /// <exception cref="InvalidOperationException">As for <see cref="Get{T}"/>.</exception>
    public unsafe ReadOnlyRef<T> GetReadOnly<T>() where T : unmanaged, IReadOnlyComponent => new((T*)PointerTo<T>());

    // The registry id of a native component type, or a clear exception.
    private static int IdOf<T>() where T : unmanaged, IReadOnlyComponent
    {
        int id = ComponentIds<T>.Id;
        if (id == ComponentIds<T>.NotNative)
            throw new InvalidOperationException($"{typeof(T).Name} is not a native component: it has no [NativeComponent] attribute.");
        if (id == ComponentIds<T>.WrongType)
            throw new InvalidOperationException($"'{ComponentIds<T>.NativeName}' is registered for a different C# type than {typeof(T).FullName}; a native component is reached only through the type the host validated.");
        if (id == ComponentIds<T>.NotRegistered)
            throw new InvalidOperationException($"The engine has no component registered as '{ComponentIds<T>.NativeName}' (for {typeof(T).Name}).");
        return id;
    }

    private unsafe void* PointerTo<T>() where T : unmanaged, IReadOnlyComponent
    {
        void* pointer = Native.ComponentGet(IdOf<T>(), Value);
        if (pointer == null)
            throw new InvalidOperationException(IsValid
                ? $"{this} has no {typeof(T).Name}."
                : $"{this} is not alive in the active scene.");
        return pointer;
    }

    public bool Equals(Entity other) => Value == other.Value;

    public override bool Equals(object? obj) => obj is Entity other && Equals(other);

    public override int GetHashCode() => Value.GetHashCode();

    public static bool operator ==(Entity left, Entity right) => left.Equals(right);

    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);

    public override string ToString() => $"Entity({Value})";
}
