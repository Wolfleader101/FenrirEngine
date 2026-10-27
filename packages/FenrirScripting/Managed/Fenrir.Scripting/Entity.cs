namespace Fenrir.Scripting;

/// <summary>
/// An entity in the active scene: the full EnTT value, version bits included, so a stale handle to a destroyed
/// entity is invalid even after its slot is reused.
/// </summary>
public readonly struct Entity : IEquatable<Entity>
{
    public Entity(uint value) => Value = value;

    /// <summary>
    /// No entity: what a C++ default <c>Entity</c> is, and what a function that finds or makes none returns. It is never
    /// valid. <c>default(Entity)</c> is not this: it names entity 0.
    /// </summary>
    public static Entity None => new(uint.MaxValue);

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
    private void Check(ChangeError code, string what, Type? component = null)
    {
        if (code == ChangeError.None)
            return;
        string subject = component == null ? "" : $" {component.Name}";
        throw new InvalidOperationException(code switch
        {
            ChangeError.EntityNotAlive => $"Cannot {what}{subject} on {this}: it is not alive in the active scene.",
            ChangeError.Unknown => $"Cannot {what}{subject} on {this}: the engine has no such component.",
            ChangeError.ReadOnly => $"Cannot {what}{subject} on {this}: scripts may only read it.",
            ChangeError.Required => $"Cannot {what}{subject} on {this}: the engine needs every entity to have it.",
            ChangeError.NotActive => $"Cannot {what}{subject} on {this}: scripting is not running.",
            _ => $"Cannot {what}{subject} on {this}: the engine refused ({code}).",
        });
    }

    /// <summary>Whether this entity is alive in the active scene.</summary>
    public unsafe bool IsValid => Native.EntityIsValid(Value);

    /// <summary>
    /// Where this entity is in the scene: its <see cref="Transform"/> composed with its parents', read at once (so it
    /// sees this system's own edits). The scale is approximate when a parent's non-uniform scale meets a child's
    /// rotation.
    /// </summary>
    /// <exception cref="InvalidOperationException">The entity is not alive, or has no <see cref="Transform"/>.</exception>
    public unsafe Transform WorldTransform
    {
        get
        {
            if (!Native.EntityWorldTransform(Value, out Transform world))
                throw NoTransform();
            return world;
        }
    }

    /// <summary>
    /// Sets this entity's <see cref="Transform"/>, relative to its parent, so that its <see cref="WorldTransform"/> is
    /// <paramref name="world"/>. It is component data, not a structural change, so it takes effect at once.
    /// </summary>
    /// <exception cref="InvalidOperationException">The entity is not alive, or has no <see cref="Transform"/>.</exception>
    public unsafe void SetWorldTransform(Transform world)
    {
        if (!Native.EntitySetWorldTransform(Value, in world))
            throw NoTransform();
    }

    private InvalidOperationException NoTransform() =>
        new(IsValid ? $"{this} has no Transform." : $"{this} is not alive in the active scene.");

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
    internal static int IdOf<T>() where T : unmanaged, IReadOnlyComponent
    {
        // A resource meets the component constraints only so that it shares Ref<T>; it never belongs to an entity.
        if (ResourceCheck<T>.IsResource)
            throw new InvalidOperationException($"{typeof(T).Name} is a resource, not a component: reach it through Resources.");
        return RegistryIds<T, ComponentKind>.Require();
    }

    // Whether a type is a native resource (an IReadOnlyResource), which is never a component.
    private static class ResourceCheck<T> where T : unmanaged, IReadOnlyComponent
    {
        internal static bool IsResource { get; } = typeof(IReadOnlyResource).IsAssignableFrom(typeof(T));
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
