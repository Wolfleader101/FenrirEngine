using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Painter;
using Fenrir.Text;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Painter` names the class and not the namespace.
namespace PainterScriptFixture;

// A HUD as C# paints it on the window overlay, from the tests outside any system: a rounded rectangle, a stroked path
// and a word. The tests make the same calls in C++ into a list of their own (Paints.cpp's CppHud), with the same
// values, submit it to the same layer, and compare what the renderer draws for each.
public static class PaintCalls
{
    public const string Word = "Speed";

    // Paints the HUD over window `window` at `layer`: 1 when the overlay gave a painter.
    public static int PaintHud(uint window, int layer)
    {
        PainterId painter = Painter.Overlay(new WindowId { Value = window }, layer);
        if (painter.Value == 0)
        {
            return 0;
        }

        Painter.DrawRoundedRect(painter, new Rect2D { X = 10, Y = 10, Width = 120, Height = 40 },
                                new CornerRadii { TopLeft = 8, TopRight = 8, BottomRight = 8, BottomLeft = 8 },
                                new Paint { Colour = new Colour(0.1f, 0.2f, 0.4f, 0.8f) });

        Path2D needle = Path2D.Create();
        needle.MoveTo(new Vector2(20, 45));
        needle.QuadTo(new Vector2(60, 5), new Vector2(110, 30));
        Painter.StrokePath(painter, needle, new StrokeStyle { Width = 3, Cap = LineCap.Round, Join = LineJoin.Round },
                           new Paint { Colour = new Colour(1, 0.5f, 0, 1) });
        needle.Destroy();

        Paragraph word = Paragraph.Create();
        word.SetText(Word, "default", new TextStyle { Size = 14, Colour = new Colour(1, 1, 1, 1) });
        word.Layout(float.PositiveInfinity);
        Painter.DrawText(painter, word, new Vector2(16, 14));
        word.Destroy();
        return 1;
    }

    // Gets a painter for the window and layer, without painting: its id, which the next PaintHud call (in another
    // system) must not be given again.
    public static ulong PainterFor(uint window, int layer) => Painter.Overlay(new WindowId { Value = window }, layer).Value;

    // Paints a rectangle through the painter whose id is `id`: after its system returned, it draws nothing.
    public static void PaintWith(ulong id) =>
        Painter.DrawRect(new PainterId { Value = id }, new Rect2D { Width = 4, Height = 4 }, new Paint());
}
