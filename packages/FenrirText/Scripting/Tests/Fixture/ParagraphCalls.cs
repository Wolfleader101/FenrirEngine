using Fenrir.Math;
using Fenrir.Text;

namespace TextScriptFixture;

// Paragraphs as C# makes them, from the tests outside any system. The tests lay the same text out in C++ in the same
// FontLibrary (Paragraphs.cpp) and compare what each reads back.
public static class ParagraphCalls
{
    // The family the tests register the font CTest names in FENRIR_TEST_TEXT_FONT as.
    public const string Family = "Roboto";
    public const string Text = "Hello, scripted world";
    public const float Size = 20.0f;

    private static Paragraph paragraph;
    private static LineMetrics[] lines = [];

    // Registers the font file CTest names as the family Roboto: 1 when it was registered.
    public static int RegisterFont() =>
        Fonts.AddFace(Family, Environment.GetEnvironmentVariable("FENRIR_TEST_TEXT_FONT") ?? "", 400, FontStyle.Normal)
            ? 1
            : 0;

    // Makes a paragraph of Text in Roboto at Size pixels, centred, and lays it out to `width` pixels (0 for no limit).
    // Returns how many lines it has.
    public static int LayOut(float width)
    {
        paragraph = Paragraph.Create();
        paragraph.SetText(Text, Family, new TextStyle { Size = Size });
        paragraph.SetStyle(new ParagraphStyle { Align = TextAlign.Center });
        paragraph.Layout(width > 0.0f ? width : float.PositiveInfinity);
        lines = Lines(paragraph);
        return lines.Length;
    }

    public static float Width() => paragraph.Size().X;
    public static float Height() => paragraph.Size().Y;

    public static float LineLeft(int line) => lines[line].Left;
    public static float LineTop(int line) => lines[line].Top;
    public static float LineBaseline(int line) => lines[line].Baseline;
    public static float LineWidth(int line) => lines[line].Width;
    public static int LineEnd(int line) => (int)lines[line].End;

    // The caret position nearest (x, y), a byte offset.
    public static int CharacterAt(float x, float y) => (int)paragraph.CharacterAt(new Vector2(x, y));

    // How many rectangles cover the bytes [begin, end), and the first one's left and width.
    public static int RangeRectCount(int begin, int end) => RangeRects(paragraph, begin, end).Length;
    public static float RangeRectLeft(int begin, int end) => RangeRects(paragraph, begin, end)[0].X;
    public static float RangeRectWidth(int begin, int end) => RangeRects(paragraph, begin, end)[0].Width;

    // Appends a second run in the default family, bold, and lays out again with no limit: the new width.
    public static float AddBoldRun()
    {
        paragraph.AddRun(" and more", "default", new TextStyle { Size = Size, Weight = 700 });
        paragraph.Layout(float.PositiveInfinity);
        return paragraph.Size().X;
    }

    // Destroys the paragraph, then calls it: each bit set is a call that did not return zero, one bit a call, so 1 is
    // the destroy alone.
    public static int DestroyThenCall()
    {
        int nonZero = paragraph.Destroy() ? 1 : 0;
        nonZero |= paragraph.Destroy() ? 2 : 0;
        nonZero |= paragraph.SetText("gone", "default", new TextStyle()) ? 4 : 0;
        nonZero |= paragraph.Layout(100.0f) ? 8 : 0;
        nonZero |= paragraph.Size() != default ? 16 : 0;
        nonZero |= paragraph.Lines(Span<LineMetrics>.Empty) != 0 ? 32 : 0;
        nonZero |= paragraph.CharacterAt(new Vector2(1, 1)) != 0 ? 64 : 0;
        nonZero |= paragraph.RangeRects(0, 4, Span<Rect2D>.Empty) != 0 ? 128 : 0;
        nonZero |= default(Paragraph).Clear() ? 256 : 0;
        return nonZero;
    }

    // The paragraph's lines, read as a script reads them: the count first, then the lines into an array of that size.
    private static LineMetrics[] Lines(Paragraph paragraph)
    {
        LineMetrics[] read = new LineMetrics[paragraph.Lines(Span<LineMetrics>.Empty)];
        paragraph.Lines(read);
        return read;
    }

    // The rectangles covering the bytes [begin, end), read the same way.
    private static Rect2D[] RangeRects(Paragraph paragraph, int begin, int end)
    {
        Rect2D[] rects = new Rect2D[paragraph.RangeRects((uint)begin, (uint)end, Span<Rect2D>.Empty)];
        paragraph.RangeRects((uint)begin, (uint)end, rects);
        return rects;
    }
}
