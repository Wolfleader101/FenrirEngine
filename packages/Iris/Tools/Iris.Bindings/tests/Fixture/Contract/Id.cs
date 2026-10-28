namespace Fixture.Bindings;

// The hand-written type a projected field shows (Fixture.binding.toml, project-as): what a script holds in place of the
// Link the mirror stores, as a scripting SDK's entity handle stands in for the engine's entity.

/// <summary>A body's id: the value of a <see cref="Link"/>, without its owner.</summary>
public readonly struct Id
{
    public readonly uint Value;

    public Id(uint value)
    {
        Value = value;
    }
}
