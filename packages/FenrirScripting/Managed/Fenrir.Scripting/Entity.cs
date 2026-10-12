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

    public bool Equals(Entity other) => Value == other.Value;

    public override bool Equals(object? obj) => obj is Entity other && Equals(other);

    public override int GetHashCode() => Value.GetHashCode();

    public static bool operator ==(Entity left, Entity right) => left.Equals(right);

    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);

    public override string ToString() => $"Entity({Value})";
}
