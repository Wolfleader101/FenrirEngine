namespace Fixture.Bindings;

// A hand-written part beside a generated mirror: behaviour only, which the generator allows.
public partial struct Body
{
    /// <summary>Whether the body is light enough to carry.</summary>
    public readonly bool IsLight => Mass < 10;

    public static float Heaviest { get; set; } = 100;
}
