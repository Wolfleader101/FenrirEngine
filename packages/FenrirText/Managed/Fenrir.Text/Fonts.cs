namespace Fenrir.Text;

// The facade over the font natives: its method is generated beside it from the natives' @facade marks
// (Generated/Fonts.cs); this part holds only the class doc.

/// <summary>
/// The app's fonts from C#: faces registered by path and family into the app's <c>FontLibrary</c>, the one the
/// engine's C++ lays its own text out in (ADR-0062). A family with no faces, or a character its faces lack, falls back
/// to the embedded default face, family <c>"default"</c>, so text never vanishes.
/// </summary>
/// <remarks>
/// Nothing here throws. With no <c>FontLibrary</c> or asset store in the app, or a file that does not load, a call
/// registers nothing and returns false; in a debug build of the engine a warning is logged once for each place in the
/// engine's native code that noticed and each reason.
/// </remarks>
public static partial class Fonts
{
}
