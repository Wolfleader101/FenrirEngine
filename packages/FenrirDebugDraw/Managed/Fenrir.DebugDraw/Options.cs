using Fenrir.Scripting;

namespace Fenrir.DebugDraw;

/// <summary>
/// How a debug draw item of a shape is kept and drawn: FenrirDebugDraw's <c>ShapeOptions</c>, with its defaults when made
/// with <c>new</c>, so a call names only what it changes:
/// <c>DebugDraw.Line(a, b, Colour.Red, new ShapeOptions { Duration = 2 })</c>.
/// </summary>
public struct ShapeOptions
{
    /// <summary>The debug group the item belongs to. <c>Default</c> when new; null counts as <c>Default</c>.</summary>
    public string? Group = "Default";
    /// <summary>
    /// How long the item is kept. 0 (the default) draws it once: until the next tick when drawn from a
    /// <see cref="Phase.Tick"/> system, otherwise until the end of this frame's drawing. A positive duration keeps it that
    /// many seconds of frame time, and <see cref="DebugDraw.Forever"/> until it is cleared. A negative one counts as 0.
    /// </summary>
    public float Duration;
    /// <summary>
    /// An entity the positions are relative to, or <see cref="Entity.None"/> (the default). The item follows the entity's
    /// world transform while it lives, and is dropped when the entity is destroyed.
    /// </summary>
    public Entity RelativeTo = Entity.None;
    /// <summary>Line thickness in pixels; 1 when new.</summary>
    public float Thickness = 1.0f;
    /// <summary>Whether the shape is depth tested (the default) or drawn on top.</summary>
    public DepthMode Depth;
    /// <summary>How many straight segments make a full circle of a round shape (at least 3); 24 when new.</summary>
    public int Segments = 24;

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
    /// <summary>How long the item is kept, as <see cref="ShapeOptions.Duration"/>.</summary>
    public float Duration;
    /// <summary>
    /// An entity the item is relative to, or <see cref="Entity.None"/> (the default). World text follows the entity's
    /// world transform; screen text and messages stay where they are. Every one is dropped when the entity is destroyed.
    /// </summary>
    public Entity RelativeTo = Entity.None;
    /// <summary>The height of the text in pixels; 16 when new.</summary>
    public float Size = 16.0f;

    public TextOptions()
    {
    }
}

// ShapeOptions and TextOptions as the natives take them: their internal mirrors, generated from
// FenrirDebugDrawScripting's ScriptOptions.hpp (Generated/NativeShapeOptions.cs and NativeTextOptions.cs), without the
// group, which crosses as text of its own. These parts only convert.
internal partial struct NativeShapeOptions
{
    public NativeShapeOptions(in ShapeOptions options)
    {
        Duration = options.Duration;
        RelativeTo = options.RelativeTo;
        Thickness = options.Thickness;
        Depth = options.Depth;
        Segments = options.Segments;
    }
}

internal partial struct NativeTextOptions
{
    public NativeTextOptions(in TextOptions options)
    {
        Duration = options.Duration;
        RelativeTo = options.RelativeTo;
        Size = options.Size;
    }
}
