namespace Fixture.Bindings;

// The hand-written part of Link, the storage behind a projected field: the member the generated getter reads, and the
// conversion the generated setter assigns an Id through.
public partial struct Link
{
    /// <summary>The id the link holds.</summary>
    public readonly Id Target => new(Value);

    /// <summary>A link to <paramref name="id"/> with no owner, so a projected field can be written from an Id.</summary>
    public static implicit operator Link(Id id) => new() { Value = id.Value };
}
