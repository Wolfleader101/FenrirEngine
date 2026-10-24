namespace Fenrir.Scripting;

/// <remarks>
/// In C#, an entity named inside a component or event (a parent, a contact's body), as the engine stores it: the entity
/// value, then the entity list the engine keeps for itself. Read <see cref="Target"/>; make one by assigning an
/// <see cref="Entity"/>.
/// </remarks>
public readonly partial struct EntityLink
{
    private EntityLink(uint value, ulong owner)
    {
        this.value = value;
        this.owner = owner;
    }

    /// <summary>
    /// The empty link, naming no entity: what a native default <c>Entity</c> is, and what <c>new EntityLink()</c> holds.
    /// A zeroed <c>default(EntityLink)</c> is not this: it names entity 0 with no scene.
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
