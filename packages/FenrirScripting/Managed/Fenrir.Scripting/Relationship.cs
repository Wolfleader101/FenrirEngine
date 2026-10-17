namespace Fenrir.Scripting;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0169, CS0649

/// <summary>
/// An entity named inside a component or event (a parent, a contact's body), as the engine stores it: the entity value,
/// then bytes the engine keeps for itself. Read <see cref="Target"/>; make one by assigning an <see cref="Entity"/>.
/// </summary>
public readonly struct EntityLink
{
    private readonly uint value;
    private readonly uint reserved;
    private readonly ulong owner;

    private EntityLink(uint value, ulong owner)
    {
        this.value = value;
        this.owner = owner;
    }

    /// <summary>
    /// The empty link, naming no entity: what a native default <c>Entity</c> is. A zeroed <c>default(EntityLink)</c> is
    /// not this: it names entity 0 with no scene.
    /// </summary>
    public static EntityLink None => new(uint.MaxValue, 0);

    /// <summary>
    /// A link to <paramref name="entity"/> in the active scene, so a native component's <c>Entity</c> field can be
    /// written from C#: <c>tether.Target = door;</c>. C# can name only entities of the active scene, including one made
    /// by <see cref="Entity.Create"/> earlier in the same system.
    /// </summary>
    public static unsafe implicit operator EntityLink(Entity entity) => new(entity.Value,
                                                                            (ulong)Native.EntityLinkOwner());

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
