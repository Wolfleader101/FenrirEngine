using Fenrir.Scripting;

namespace Fenrir.DebugDraw;

// Each public options type holds the options as the natives take them, its generated internal mirror
// (Generated/NativeShapeOptions.cs and NativeTextOptions.cs, from FenrirDebugDrawScripting's ScriptOptions.hpp), and
// forwards to it, so its defaults are the description's and nothing converts between the two; the group, which no mirror
// can hold, crosses as text of its own (ADR-0043).

/// <summary>
/// How a debug draw item of a shape is kept and drawn: FenrirDebugDraw's <c>ShapeOptions</c>, with its defaults when made
/// with <c>new</c>, so a call names only what it changes:
/// <c>DebugDraw.Line(a, b, Colour.Red, new ShapeOptions { Duration = 2 })</c>.
/// </summary>
public struct ShapeOptions
{
    /// <summary>The debug group the item belongs to. <c>Default</c> when new; null counts as <c>Default</c>.</summary>
    public string? Group = "Default";

    /// <summary>The options as the natives take them: every field but the group, with the description's defaults.</summary>
    internal NativeShapeOptions Native = new();

    /// <summary>
    /// How long the item is kept. 0 (the default) draws it once: until the next tick when drawn from a
    /// <see cref="Phase.Tick"/> system, otherwise until the end of this frame's drawing. A positive duration keeps it that
    /// many seconds of frame time, and <see cref="DebugDraw.Forever"/> until it is cleared. A negative one counts as 0.
    /// </summary>
    public float Duration
    {
        get => Native.Duration;
        set => Native.Duration = value;
    }

    /// <summary>
    /// An entity the positions are relative to, or <see cref="Entity.None"/> (the default). The item follows the entity's
    /// world transform while it lives, and is dropped when the entity is destroyed.
    /// </summary>
    public Entity RelativeTo
    {
        get => Native.RelativeTo;
        set => Native.RelativeTo = value;
    }

    /// <summary>Line thickness in pixels; 1 when new.</summary>
    public float Thickness
    {
        get => Native.Thickness;
        set => Native.Thickness = value;
    }

    /// <summary>Whether the shape is depth tested (the default) or drawn on top.</summary>
    public DepthMode Depth
    {
        get => Native.Depth;
        set => Native.Depth = value;
    }

    /// <summary>How many straight segments make a full circle of a round shape (at least 3); 24 when new.</summary>
    public int Segments
    {
        get => Native.Segments;
        set => Native.Segments = value;
    }

    public ShapeOptions()
    {
    }
}

/// <summary>
/// How a debug draw item of text is kept and drawn: FenrirDebugDraw's <c>TextOptions</c>, as <see cref="ShapeOptions"/>
/// without the shape-only fields, with its defaults when made with <c>new</c>.
/// </summary>
public struct TextOptions
{
    /// <summary>The debug group the item belongs to. <c>Default</c> when new; null counts as <c>Default</c>.</summary>
    public string? Group = "Default";

    /// <summary>The options as the natives take them: every field but the group, with the description's defaults.</summary>
    internal NativeTextOptions Native = new();

    /// <summary>How long the item is kept, as <see cref="ShapeOptions.Duration"/>.</summary>
    public float Duration
    {
        get => Native.Duration;
        set => Native.Duration = value;
    }

    /// <summary>
    /// An entity the item is relative to, or <see cref="Entity.None"/> (the default). World text follows the entity's
    /// world transform; screen text and messages stay where they are. Every one is dropped when the entity is destroyed.
    /// </summary>
    public Entity RelativeTo
    {
        get => Native.RelativeTo;
        set => Native.RelativeTo = value;
    }

    /// <summary>The height of the text in pixels; 16 when new.</summary>
    public float Size
    {
        get => Native.Size;
        set => Native.Size = value;
    }

    public TextOptions()
    {
    }
}
