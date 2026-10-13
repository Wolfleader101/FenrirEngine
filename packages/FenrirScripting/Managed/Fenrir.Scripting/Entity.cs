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

    /// <summary>
    /// A new entity in the active scene. Its value is usable at once (to add components to, say). Like every structural
    /// change it takes effect when the system returns: until then it is alive but has no components, not even a
    /// <see cref="Transform"/>.
    /// </summary>
    /// <exception cref="InvalidOperationException">There is no active scene.</exception>
    public static unsafe Entity Create()
    {
        uint value = Native.EntityCreate();
        if (value == uint.MaxValue)
            throw new InvalidOperationException("There is no active scene to create an entity in.");
        return new Entity(value);
    }

    /// <summary>
    /// Destroys this entity, and with it all its components, when the system returns. Until then it is still alive and
    /// its components are still readable. Destroying one twice is the same as once.
    /// </summary>
    /// <exception cref="InvalidOperationException">The entity is not alive now.</exception>
    public unsafe void Destroy() => Check(Native.EntityDestroy(Value), "destroy");

    /// <summary>
    /// Gives this entity a default (all-zero, for a script component) <typeparamref name="T"/> when the system returns,
    /// keeping the one it has if it has one. Until then <see cref="Has{T}"/> still says what it did.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    /// The entity is not alive now, or the type is not a component the engine has registered.
    /// </exception>
    public unsafe void Add<T>() where T : unmanaged, IComponent => Check(Native.ComponentAdd(IdOf<T>(), Value, null), "add", typeof(T));

    /// <summary>
    /// Gives this entity <paramref name="value"/> as its <typeparamref name="T"/> when the system returns, replacing the one
    /// it has if it has one.
    /// </summary>
    /// <exception cref="InvalidOperationException">As for <see cref="Add{T}()"/>.</exception>
    public unsafe void Add<T>(T value) where T : unmanaged, IComponent =>
        Check(Native.ComponentAdd(IdOf<T>(), Value, &value), "add", typeof(T));

    /// <summary>
    /// Takes <typeparamref name="T"/> off this entity when the system returns. Removing one it does not have does nothing.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    /// The entity is not alive now, the type is not registered, or the engine requires every entity to have it
    /// (<see cref="Transform"/>, <see cref="Name"/>).
    /// </exception>
    public unsafe void Remove<T>() where T : unmanaged, IComponent => Check(Native.ComponentRemove(IdOf<T>(), Value), "remove", typeof(T));

    // Turns the engine's reason for refusing a change into an exception that says what was attempted.
    private void Check(int code, string what, Type? component = null)
    {
        if (code == 0)
            return;
        string subject = component == null ? "" : $" {component.Name}";
        throw new InvalidOperationException(code switch
        {
            -1 => $"Cannot {what}{subject} on {this}: it is not alive in the active scene.",
            -2 => $"Cannot {what}{subject} on {this}: the engine has no such component.",
            -3 => $"Cannot {what}{subject} on {this}: scripts may only read it.",
            -4 => $"Cannot {what}{subject} on {this}: the engine needs every entity to have it.",
            -5 => $"Cannot {what}{subject} on {this}: scripting is not running.",
            _ => $"Cannot {what}{subject} on {this}: the engine refused (code {code}).",
        });
    }

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
        if (id == ComponentIds<T>.WrongType)
            throw new InvalidOperationException($"'{ComponentIds<T>.NativeName}' is registered for a different C# type than {typeof(T).FullName}; a native component is reached only through the type the host validated.");
        if (id == ComponentIds<T>.NotRegistered)
            throw new InvalidOperationException(ComponentIds<T>.NativeName != null
                ? $"The engine has no component registered as '{ComponentIds<T>.NativeName}' (for {typeof(T).Name})."
                : $"The engine does not know {typeof(T).Name}: a component a script defines is registered when its assembly loads, and only if the script project uses the Fenrir scripting SDK.");
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
