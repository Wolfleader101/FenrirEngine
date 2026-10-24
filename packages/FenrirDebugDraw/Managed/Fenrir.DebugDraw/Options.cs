using Fenrir.Scripting;

namespace Fenrir.DebugDraw;

/// <summary>Whether a shape is hidden behind what is in front of it: FenrirDebugDraw's <c>DepthMode</c>.</summary>
public enum DepthMode : byte
{
    /// <summary>Tested against the scene's depth, so geometry in front hides it (the default).</summary>
    Tested = 0,
    /// <summary>Drawn over everything, never hidden.</summary>
    OnTop = 1,
}

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

// ShapeOptions and TextOptions without their group, which crosses as text of its own: laid out exactly as
// FenrirDebugDrawScripting's native mirrors, with the entity as its 32-bit value and the depth mode widened to 32 bits so
// there is no padding to agree on. The native side checks its sizes at compile time, and the tests read every field back
// through what each call draws.
internal struct NativeShapeOptions
{
    public float Duration;
    public uint RelativeTo;
    public float Thickness;
    public uint Depth;
    public int Segments;

    public NativeShapeOptions(in ShapeOptions options)
    {
        Duration = options.Duration;
        RelativeTo = options.RelativeTo.Value;
        Thickness = options.Thickness;
        Depth = (uint)options.Depth;
        Segments = options.Segments;
    }
}

internal struct NativeTextOptions
{
    public float Duration;
    public uint RelativeTo;
    public float Size;

    public NativeTextOptions(in TextOptions options)
    {
        Duration = options.Duration;
        RelativeTo = options.RelativeTo.Value;
        Size = options.Size;
    }
}
