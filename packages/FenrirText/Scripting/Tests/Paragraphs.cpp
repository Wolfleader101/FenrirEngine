// Paragraphs made and laid out from C# (spec: "Scripting"; seam 1 through the scripting harness): a script registers a
// font by path and family in the app's FontLibrary, lays a paragraph out and reads its size, lines and hit positions,
// which are what C++ reads from the same text laid out in the same library; and a destroyed paragraph's calls do
// nothing and return zero.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>

#include "FenrirMath/Math.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirText/TextStyle.hpp"
#include "TextScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    // The fixture's text, family and size (ParagraphCalls).
    const std::string Text = "Hello, scripted world";
    constexpr float Size = 20.0f;

    // The fixture's paragraph, made in C++: Text in Roboto at Size pixels, centred, laid out to `width` (0 for none).
    Paragraph CppParagraph(FontLibrary& fonts, float width)
    {
        Paragraph paragraph;
        TextStyle style;
        style.family = "Roboto";
        style.size = Size;
        paragraph.SetText(Text, style);
        ParagraphStyle layout;
        layout.align = TextAlign::Center;
        paragraph.SetStyle(layout);
        paragraph.Layout(fonts, width > 0.0f ? width : UnlimitedWidth);
        return paragraph;
    }

    // Registers the font CTest names as Roboto from C#, requiring it to register.
    void RegisterFont(TextScriptingApp& test)
    {
        INFO("Fonts.AddFace should register the font file CTest names in FENRIR_TEST_TEXT_FONT");
        REQUIRE(test.Call<int>(ParagraphCallsType, "RegisterFont") == 1);
    }
} // namespace

TEST_CASE("A script lays out a paragraph and reads the size C++ reads for the same text", "[text][scripting]")
{
    TextScriptingApp test;
    RegisterFont(test);
    for (const float width : {0.0f, 80.0f})
    {
        const Paragraph cpp = CppParagraph(test.Fonts(), width);
        const int lines = test.Call<int>(ParagraphCallsType, "LayOut", width);
        INFO("Laid out to " << width << " pixels (0 for no limit), C# should read the lines and size C++ reads");
        REQUIRE(lines == static_cast<int>(cpp.Lines().size()));
        REQUIRE(test.Call<float>(ParagraphCallsType, "Width") == cpp.Size().x);
        REQUIRE(test.Call<float>(ParagraphCallsType, "Height") == cpp.Size().y);
        REQUIRE(cpp.Size().x > 0.0f);
        if (width > 0.0f)
        {
            INFO("The narrow width should wrap the text");
            REQUIRE(lines > 1);
        }
    }
}

TEST_CASE("A script reads a paragraph's lines and hit positions as C++ reads them", "[text][scripting]")
{
    TextScriptingApp test;
    RegisterFont(test);
    constexpr float width = 80.0f;
    const Paragraph cpp = CppParagraph(test.Fonts(), width);
    {
        INFO("C# should lay the text out into as many lines as C++ does");
        REQUIRE(test.Call<int>(ParagraphCallsType, "LayOut", width) == static_cast<int>(cpp.Lines().size()));
    }

    const std::span<const LineMetrics> lines = cpp.Lines();
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        const int index = static_cast<int>(line);
        INFO("Line " << line << ": C# should read the metrics C++ reads");
        REQUIRE(test.Call<float>(ParagraphCallsType, "LineLeft", index) == lines[line].left);
        REQUIRE(test.Call<float>(ParagraphCallsType, "LineTop", index) == lines[line].top);
        REQUIRE(test.Call<float>(ParagraphCallsType, "LineBaseline", index) == lines[line].baseline);
        REQUIRE(test.Call<float>(ParagraphCallsType, "LineWidth", index) == lines[line].width);
        REQUIRE(test.Call<int>(ParagraphCallsType, "LineEnd", index) == static_cast<int>(lines[line].end));
    }

    INFO("C# should find the caret and range rectangles C++ finds");
    const Math::Vec2 point{lines[0].left + 20.0f, lines[0].top + 2.0f};
    REQUIRE(test.Call<int>(ParagraphCallsType, "CharacterAt", point.x, point.y) ==
            static_cast<int>(cpp.CharacterAt(point)));
    REQUIRE(test.Call<int>(ParagraphCallsType, "CharacterAt", 1000.0f, 1000.0f) ==
            static_cast<int>(cpp.CharacterAt({1000.0f, 1000.0f})));

    const std::vector<Rect2D> range = cpp.RangeRects(2, 9);
    REQUIRE_FALSE(range.empty());
    REQUIRE(test.Call<int>(ParagraphCallsType, "RangeRectCount", 2, 9) == static_cast<int>(range.size()));
    REQUIRE(test.Call<float>(ParagraphCallsType, "RangeRectLeft", 2, 9) == range[0].x);
    REQUIRE(test.Call<float>(ParagraphCallsType, "RangeRectWidth", 2, 9) == range[0].width);
}

TEST_CASE("A run a script appends lays out with the paragraph's", "[text][scripting]")
{
    TextScriptingApp test;
    RegisterFont(test);
    {
        INFO("The fixture's text should lay out on one line with no width limit");
        REQUIRE(test.Call<int>(ParagraphCallsType, "LayOut", 0.0f) == 1);
    }
    const float before = test.Call<float>(ParagraphCallsType, "Width");

    Paragraph cpp = CppParagraph(test.Fonts(), 0.0f);
    TextStyle bold;
    bold.size = Size;
    bold.weight = FontWeightBold;
    cpp.AddRun(" and more", bold);
    cpp.Layout(test.Fonts(), UnlimitedWidth);

    const float after = test.Call<float>(ParagraphCallsType, "AddBoldRun");
    INFO("The appended bold run should widen the paragraph as it widens C++'s");
    REQUIRE(after > before);
    REQUIRE(after == cpp.Size().x);
}

TEST_CASE("A destroyed paragraph's calls do nothing and return zero", "[text][scripting]")
{
    TextScriptingApp test;
    {
        INFO("The fixture's text should lay out on one line with no width limit");
        REQUIRE(test.Call<int>(ParagraphCallsType, "LayOut", 0.0f) == 1);
    }
    INFO("Only the first Destroy should return non-zero; each other bit is a call on the destroyed paragraph");
    REQUIRE(test.Call<int>(ParagraphCallsType, "DestroyThenCall") == 1);
}
