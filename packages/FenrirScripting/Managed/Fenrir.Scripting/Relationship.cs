namespace Fenrir.Scripting;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0169, CS0649

/// <summary>
/// A link to another entity in the hierarchy, as the engine stores it: the entity value, then bytes the engine keeps for
/// itself. Read <see cref="Target"/>; only the engine edits the hierarchy.
/// </summary>
public readonly struct EntityLink
{
    private readonly uint value;
    private readonly uint reserved;
    private readonly ulong owner;

    /// <summary>Whether this links to nothing.</summary>
    public bool IsNull => value == uint.MaxValue;

    /// <summary>The entity linked to. Only meaningful when <see cref="IsNull"/> is false.</summary>
    public Entity Target => new(value);
}

/// <summary>
/// Where an entity sits in the hierarchy: the engine's <c>Fenrir::Relationship</c>. Scripts can read it but never write
/// it, because the engine keeps the links consistent: it is an <see cref="IReadOnlyComponent"/>, so it is reached through
/// <see cref="ReadOnlyRef{T}"/> and has no <see cref="Ref{T}"/>.
/// </summary>
[NativeComponent("Fenrir.Relationship")]
public readonly struct Relationship : IReadOnlyComponent
{
    public readonly EntityLink Parent;
    public readonly EntityLink FirstChild;
    public readonly EntityLink NextSibling;
    public readonly EntityLink PrevSibling;
}
