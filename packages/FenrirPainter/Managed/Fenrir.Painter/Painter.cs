namespace Fenrir.Painter;

// The facade over the generated PainterNative (Generated/, from Scripting/src/PainterScripting.cpp). Every method is
// generated beside it from the natives' @facade marks (Generated/Painter.cs); this part holds only the class doc.

/// <summary>
/// The painter from C#: one native function per FenrirPainter call (ADR-0060, ADR-0061; its C++ documentation says what
/// each draws). A C# system is given a painter over a window's overlay at one layer (<see cref="Overlay"/>), paints
/// through it, and what it painted is drawn over the game that frame, submitted when the system returns. Shapes are
/// analytic and anti-aliased, paths are filled and stroked with a one-pixel fringe, text is FenrirText's glyphs, and
/// offscreen groups, masks and backdrop blurs are recorded for the renderer to realise.
/// </summary>
/// <remarks>
/// Coordinates are the window's logical units. Paths are <see cref="Path2D"/>s, or spans of points for a polygon or
/// polyline drawn once; text is a <see cref="Fenrir.Text.Paragraph"/>.
/// <para>
/// Nothing here throws. With no overlay in the app (the renderer not installed) or no <c>builtin:painter</c> in its
/// asset store, <see cref="Overlay"/> gives no painter; a painter whose system returned or whose frame is over, a path destroyed or a paragraph
/// destroyed draws nothing. In a debug build of the engine a warning is logged once for each place in the engine's
/// native code that noticed and each reason.
/// </para>
/// </remarks>
public static partial class Painter
{
}
