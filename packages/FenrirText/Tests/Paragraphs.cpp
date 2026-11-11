// Paragraph output (the spec's seam 3): text, styles and a width in; glyph runs, lines, bidi order, alignment,
// ellipsis, hit positions and grapheme boundaries out. Widths come from the embedded face, JetBrains Mono, whose every
// glyph advances 600 units of its 1000-unit em: 6 pixels at 10 pixels.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "TextTest.hpp"

using Catch::Approx;
using TextTest::FullFont;
using TextTest::Glyphs;
using TextTest::Style;
using TextTest::TextApp;
using TextTest::Utf8;

namespace
{
    // The clusters of the paragraph's glyphs, left to right.
    std::vector<std::uint32_t> Clusters(const Fenrir::Paragraph& paragraph)
    {
        std::vector<std::uint32_t> clusters;
        for (const Fenrir::PositionedGlyph& glyph : Glyphs(paragraph))
        {
            clusters.push_back(glyph.cluster);
        }
        return clusters;
    }

    // The x of the first glyph drawn for the character at byte `cluster`.
    float XOf(const Fenrir::Paragraph& paragraph, std::uint32_t cluster)
    {
        for (const Fenrir::PositionedGlyph& glyph : Glyphs(paragraph))
        {
            if (glyph.cluster == cluster)
            {
                return glyph.origin.x;
            }
        }
        return -1.0f;
    }

    Fenrir::Paragraph Laid(TextApp& test, const std::string& text, const Fenrir::TextStyle& style, float width,
                           Fenrir::ParagraphStyle paragraphStyle = {})
    {
        Fenrir::Paragraph paragraph;
        paragraph.SetStyle(paragraphStyle);
        paragraph.SetText(text, style);
        paragraph.Layout(test.fonts, width);
        return paragraph;
    }
} // namespace

TEST_CASE("Latin text is kerned and ligated", "[text][paragraph][shaping]")
{
    TextApp test;
    test.Register("Roboto", FullFont("Roboto-Regular.ttf"));

    const Fenrir::Paragraph alone = Laid(test, "A", Style("Roboto", 40.0f), Fenrir::UnlimitedWidth);
    const Fenrir::Paragraph pair = Laid(test, "AV", Style("Roboto", 40.0f), Fenrir::UnlimitedWidth);
    INFO("V starts left of where A alone ends: the pair is kerned");
    REQUIRE(XOf(pair, 1) < alone.Size().x - 0.5f);

    const Fenrir::Paragraph fi = Laid(test, "fi", Style("Roboto", 40.0f), Fenrir::UnlimitedWidth);
    INFO("f and i shape to one ligature glyph, of the first character's cluster");
    REQUIRE(Clusters(fi) == std::vector<std::uint32_t>{0});

    Fenrir::TextStyle spaced = Style("Roboto", 40.0f);
    spaced.letterSpacing = 2.0f;
    const Fenrir::Paragraph apart = Laid(test, "fi", spaced, Fenrir::UnlimitedWidth);
    INFO("letter spacing turns the ligature off, so both letters are drawn");
    REQUIRE(Clusters(apart) == std::vector<std::uint32_t>{0, 1});
}

TEST_CASE("An Arabic run is shaped and ordered right to left", "[text][paragraph][bidi]")
{
    TextApp test;
    test.Register("Amiri", FullFont("Amiri-Regular.ttf"));
    // Salaam: seen, lam, alef, meem, two bytes each.
    const Fenrir::Paragraph paragraph = Laid(test, Utf8(u8"\u0633\u0644\u0627\u0645"), Style("Amiri", 40.0f), 400.0f);

    const std::vector<std::uint32_t> clusters = Clusters(paragraph);
    const Fenrir::Paragraph alone = Laid(test, Utf8(u8"\u0633"), Style("Amiri", 40.0f), 400.0f);
    INFO("seen joined to lam takes its initial form, another glyph than seen alone");
    REQUIRE(Glyphs(paragraph).back().cluster == 0);
    REQUIRE(Glyphs(paragraph).back().glyph != Glyphs(alone).front().glyph);
    INFO("the leftmost glyph is the last letter, meem, and clusters fall from left to right");
    REQUIRE(clusters.front() == 6);
    for (std::size_t i = 1; i < clusters.size(); ++i)
    {
        REQUIRE(clusters[i] < clusters[i - 1]);
    }
    INFO("its runs read right to left");
    REQUIRE(paragraph.Runs().front().rightToLeft);
    INFO("a right-to-left paragraph starts at the right: Start alignment puts its line against the right edge");
    REQUIRE(paragraph.Lines()[0].left + paragraph.Lines()[0].width == Approx(400.0f));
}

TEST_CASE("A mixed-direction line keeps each run's order", "[text][paragraph][bidi]")
{
    TextApp test;
    test.Register("Amiri", FullFont("Amiri-Regular.ttf"));
    // "ab ", then salaam (bytes 3 to 10), then " cd" (c at byte 12).
    const std::string text = Utf8(u8"ab \u0633\u0644\u0627\u0645 cd");
    const Fenrir::Paragraph paragraph = Laid(test, text, Style("Amiri", 30.0f), Fenrir::UnlimitedWidth);

    INFO("in a left-to-right paragraph the Latin before the Arabic is left of it, and the Latin after is right of it");
    const float a = XOf(paragraph, 0);
    const float meem = XOf(paragraph, 9);
    const float seen = XOf(paragraph, 3);
    const float c = XOf(paragraph, 12);
    REQUIRE(a < meem);
    INFO("within the Arabic, meem (read last) is left of seen (read first)");
    REQUIRE(meem < seen);
    REQUIRE(seen < c);

    Fenrir::ParagraphStyle rtl;
    rtl.direction = Fenrir::TextDirection::RightToLeft;
    const Fenrir::Paragraph flipped = Laid(test, text, Style("Amiri", 30.0f), Fenrir::UnlimitedWidth, rtl);
    INFO("in a right-to-left paragraph the runs go right to left: cd is leftmost and ab rightmost");
    REQUIRE(XOf(flipped, 12) < XOf(flipped, 9));
    REQUIRE(XOf(flipped, 3) < XOf(flipped, 0));
}

TEST_CASE("A Devanagari conjunct shapes to one glyph", "[text][paragraph][shaping]")
{
    TextApp test;
    test.Register("Devanagari", FullFont("NotoSansDevanagari-Regular.ttf"));
    // Ka, virama, ssa: the conjunct kssa.
    const Fenrir::Paragraph paragraph =
        Laid(test, Utf8(u8"\u0915\u094D\u0937"), Style("Devanagari", 32.0f), Fenrir::UnlimitedWidth);
    INFO("the three characters draw as one glyph, of the cluster at byte 0");
    REQUIRE(Clusters(paragraph) == std::vector<std::uint32_t>{0});

    // Ka and the vowel sign i, which is drawn before the consonant it follows.
    const Fenrir::Paragraph ki = Laid(test, Utf8(u8"\u0915\u093F"), Style("Devanagari", 32.0f), Fenrir::UnlimitedWidth);
    const std::vector<Fenrir::PositionedGlyph> glyphs = Glyphs(ki);
    INFO("ki draws two glyphs of one cluster");
    REQUIRE(glyphs.size() == 2);
    REQUIRE(glyphs[0].cluster == 0);
    REQUIRE(glyphs[1].cluster == 0);
}

TEST_CASE("Text wraps at the width, at break opportunities", "[text][paragraph][lines]")
{
    TextApp test;
    // 6 pixels a character: "aaa bbb" is 42 pixels, and " ccc" would take it to 66.
    const Fenrir::Paragraph paragraph = Laid(test, "aaa bbb ccc", Style("default", 10.0f), 50.0f);

    const auto lines = paragraph.Lines();
    INFO("two lines: 'aaa bbb ' and 'ccc'");
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].begin == 0);
    REQUIRE(lines[0].end == 8);
    REQUIRE(lines[1].begin == 8);
    REQUIRE(lines[1].end == 11);
    INFO("the first line's width leaves out its trailing space");
    REQUIRE(lines[0].width == Approx(42.0f));
    INFO("the paragraph is as wide as its widest line");
    REQUIRE(paragraph.Size().x == Approx(42.0f));

    const Fenrir::Paragraph unlimited = Laid(test, "aaa bbb ccc", Style("default", 10.0f), Fenrir::UnlimitedWidth);
    INFO("with no width it stays on one line");
    REQUIRE(unlimited.Lines().size() == 1);
}

TEST_CASE("Newlines break lines only where white space is kept", "[text][paragraph][lines]")
{
    TextApp test;
    Fenrir::ParagraphStyle pre;
    pre.whiteSpace = Fenrir::WhiteSpace::Pre;
    const Fenrir::Paragraph kept = Laid(test, "ab\ncd", Style("default", 10.0f), Fenrir::UnlimitedWidth, pre);
    INFO("under Pre a newline breaks the line");
    REQUIRE(kept.Lines().size() == 2);

    const Fenrir::Paragraph collapsed = Laid(test, "ab\n\n  cd", Style("default", 10.0f), Fenrir::UnlimitedWidth);
    INFO("under Normal the newlines and spaces collapse to one space: 'ab cd' is 30 pixels");
    REQUIRE(collapsed.Lines().size() == 1);
    REQUIRE(collapsed.Size().x == Approx(30.0f));
}

TEST_CASE("Cut-off text ends its last line with an ellipsis", "[text][paragraph][lines]")
{
    TextApp test;
    Fenrir::ParagraphStyle one;
    one.maxLines = 1;
    one.ellipsis = true;
    const Fenrir::Paragraph limited = Laid(test, "aaa bbb ccc", Style("default", 10.0f), 50.0f, one);
    INFO("one line is kept");
    REQUIRE(limited.Lines().size() == 1);
    INFO("'aaa bbb' and the ellipsis: seven letters and one more glyph, 48 pixels");
    REQUIRE(Glyphs(limited).size() == 7);
    REQUIRE(limited.Size().x == Approx(48.0f));

    Fenrir::ParagraphStyle noWrap;
    noWrap.whiteSpace = Fenrir::WhiteSpace::NoWrap;
    noWrap.ellipsis = true;
    const Fenrir::Paragraph cut = Laid(test, "aaa bbb ccc", Style("default", 10.0f), 30.0f, noWrap);
    INFO("an unwrapped line past 30 pixels keeps what fits with the ellipsis: 'aaa' and the ellipsis, 24 pixels");
    REQUIRE(cut.Lines().size() == 1);
    REQUIRE(Glyphs(cut).size() == 4);
    REQUIRE(cut.Size().x == Approx(24.0f));

    Fenrir::ParagraphStyle twoLines;
    twoLines.maxLines = 2;
    const Fenrir::Paragraph truncated = Laid(test, "aaa bbb ccc ddd eee", Style("default", 10.0f), 30.0f, twoLines);
    INFO("without an ellipsis, maxLines only stops after two lines");
    REQUIRE(truncated.Lines().size() == 2);
    REQUIRE(Glyphs(truncated).size() == 6);
}

TEST_CASE("Lines are aligned across the width", "[text][paragraph][align]")
{
    TextApp test;
    // "abc" is 18 pixels in a 100-pixel width.
    Fenrir::ParagraphStyle style;
    style.align = Fenrir::TextAlign::Left;
    REQUIRE(XOf(Laid(test, "abc", Style("default", 10.0f), 100.0f, style), 0) == Approx(0.0f));
    style.align = Fenrir::TextAlign::Center;
    INFO("centred, it starts (100 - 18) / 2 in");
    REQUIRE(XOf(Laid(test, "abc", Style("default", 10.0f), 100.0f, style), 0) == Approx(41.0f));
    style.align = Fenrir::TextAlign::Right;
    INFO("right-aligned, it starts 100 - 18 in");
    REQUIRE(XOf(Laid(test, "abc", Style("default", 10.0f), 100.0f, style), 0) == Approx(82.0f));
    style.align = Fenrir::TextAlign::End;
    INFO("End is right in a left-to-right paragraph");
    REQUIRE(XOf(Laid(test, "abc", Style("default", 10.0f), 100.0f, style), 0) == Approx(82.0f));
}

TEST_CASE("Line metrics stack line boxes of the line height", "[text][paragraph][lines]")
{
    TextApp test;
    Fenrir::ParagraphStyle style;
    style.lineHeight = 2.0f;
    const Fenrir::Paragraph paragraph = Laid(test, "aaa bbb", Style("default", 10.0f), 30.0f, style);

    const auto lines = paragraph.Lines();
    REQUIRE(lines.size() == 2);
    INFO("a line height of 2 at 10 pixels makes 20-pixel line boxes, one under the other");
    REQUIRE(lines[0].top == Approx(0.0f));
    REQUIRE(lines[0].height == Approx(20.0f));
    REQUIRE(lines[1].top == Approx(20.0f));
    REQUIRE(paragraph.Size().y == Approx(40.0f));
    INFO("the baseline sits inside its box, the face's ascent below the half-leading");
    REQUIRE(lines[0].baseline > lines[0].top);
    REQUIRE(lines[0].baseline - lines[0].ascent ==
            Approx(lines[0].top + (lines[0].height - lines[0].ascent - lines[0].descent) / 2.0f));
    INFO("glyphs of the second line sit on its baseline");
    REQUIRE(Glyphs(paragraph).back().origin.y == Approx(lines[1].baseline));
}

TEST_CASE("Character rectangles and hit testing agree", "[text][paragraph][hit]")
{
    TextApp test;
    const Fenrir::Paragraph paragraph = Laid(test, "abc def", Style("default", 10.0f), Fenrir::UnlimitedWidth);
    const float height = paragraph.Lines()[0].height;

    INFO("b is the 6-pixel box from 6 to 12, a line tall");
    REQUIRE(paragraph.RangeRects(1, 2) == std::vector<Fenrir::Rect2D>{{6.0f, 0.0f, 6.0f, height}});
    INFO("a range across the space is one box");
    REQUIRE(paragraph.RangeRects(1, 5) == std::vector<Fenrir::Rect2D>{{6.0f, 0.0f, 24.0f, height}});

    for (std::uint32_t i = 0; i < 7; ++i)
    {
        const Fenrir::Rect2D box = paragraph.RangeRects(i, i + 1).front();
        INFO("a point in the first half of character " << i << " is caret position " << i);
        REQUIRE(paragraph.CharacterAt({box.x + box.width * 0.25f, box.y + box.height * 0.5f}) == i);
        INFO("a point in its second half is the position after it");
        REQUIRE(paragraph.CharacterAt({box.x + box.width * 0.75f, box.y + box.height * 0.5f}) == i + 1);
    }
    INFO("left of the line is its start, right of it its end");
    REQUIRE(paragraph.CharacterAt({-50.0f, 1.0f}) == 0);
    REQUIRE(paragraph.CharacterAt({500.0f, 1.0f}) == 7);
}

TEST_CASE("Hit testing in right-to-left text finds each character's leading edge on its right",
          "[text][paragraph][hit]")
{
    TextApp test;
    test.Register("Amiri", FullFont("Amiri-Regular.ttf"));
    // Beh, teh, theh: three letters that join but form no ligature, two bytes each.
    const Fenrir::Paragraph paragraph =
        Laid(test, Utf8(u8"\u0628\u062A\u062B"), Style("Amiri", 40.0f), Fenrir::UnlimitedWidth);
    for (std::uint32_t i = 0; i < 6; i += 2)
    {
        const std::vector<Fenrir::Rect2D> boxes = paragraph.RangeRects(i, i + 2);
        REQUIRE(boxes.size() == 1);
        const Fenrir::Rect2D box = boxes.front();
        INFO("a point in the right half of letter " << i / 2 << " is the caret before it");
        REQUIRE(paragraph.CharacterAt({box.x + box.width * 0.75f, box.y + box.height * 0.5f}) == i);
        INFO("a point in its left half is the caret after it");
        REQUIRE(paragraph.CharacterAt({box.x + box.width * 0.25f, box.y + box.height * 0.5f}) == i + 2);
    }
    INFO("the first letter is the rightmost");
    REQUIRE(paragraph.RangeRects(0, 2).front().x > paragraph.RangeRects(4, 6).front().x);
}

TEST_CASE("Grapheme boundaries keep combining marks and joined emoji with their base", "[text][paragraph][graphemes]")
{
    Fenrir::Paragraph paragraph;
    // e and a combining acute (bytes 0 to 2), x (byte 3), then a family emoji joined by zero-width joiners (bytes 4 to
    // 21), then y.
    paragraph.SetText(Utf8(u8"e\u0301x\U0001F468\u200D\U0001F469\u200D\U0001F467y"), Style("default"));
    INFO("e with its accent is one grapheme, the joined family another");
    REQUIRE(std::vector<std::uint32_t>(paragraph.GraphemeBoundaries().begin(), paragraph.GraphemeBoundaries().end()) ==
            std::vector<std::uint32_t>{0, 3, 4, 22, 23});
    INFO("stepping from inside the accent moves to the grapheme's edges");
    REQUIRE(paragraph.NextGrapheme(1) == 3);
    REQUIRE(paragraph.PreviousGrapheme(3) == 0);
    REQUIRE(paragraph.NextGrapheme(4) == 22);
    REQUIRE(paragraph.PreviousGrapheme(0) == 0);
    REQUIRE(paragraph.NextGrapheme(23) == 23);
}

TEST_CASE("A paragraph lays out again only when its text, style or width changes", "[text][paragraph]")
{
    TextApp test;
    Fenrir::Paragraph paragraph;
    paragraph.SetText("some words", Style("default", 10.0f));
    INFO("the first layout lays out");
    REQUIRE(paragraph.Layout(test.fonts, 100.0f));
    INFO("the same width again does nothing");
    REQUIRE_FALSE(paragraph.Layout(test.fonts, 100.0f));
    INFO("a new width lays out");
    REQUIRE(paragraph.Layout(test.fonts, 40.0f));
    REQUIRE_FALSE(paragraph.Layout(test.fonts, 40.0f));

    paragraph.SetStyle({Fenrir::TextAlign::Center});
    INFO("a new paragraph style lays out");
    REQUIRE(paragraph.Layout(test.fonts, 40.0f));
    paragraph.AddRun(" more", Style("default", 12.0f));
    INFO("new text lays out");
    REQUIRE(paragraph.Layout(test.fonts, 40.0f));
    REQUIRE_FALSE(paragraph.Layout(test.fonts, 40.0f));
}

TEST_CASE("Glyphs are rasterised into their face's atlas at quarter-pixel positions", "[text][paragraph][atlas]")
{
    TextApp test;
    const Fenrir::Paragraph paragraph = Laid(test, "Hi", Style("default", 10.5f), Fenrir::UnlimitedWidth);
    const Fenrir::GlyphRun& run = paragraph.Runs().front();

    INFO("the run's page is the atlas of the default face at 10.5 pixels");
    REQUIRE(test.fonts.AtlasPages(test.fonts.DefaultFont(), 10.5f).front() == run.page);
    INFO("i's pen position is H's advance, 6.3 pixels, rounded to a quarter pixel: 6.25");
    REQUIRE(run.glyphs[1].origin.x == Approx(6.25f));
    for (const Fenrir::PositionedGlyph& glyph : run.glyphs)
    {
        INFO("each glyph's quad is the size of its atlas rectangle, in whole pixels");
        REQUIRE(glyph.quad.width == static_cast<float>(glyph.atlas.region.width));
        REQUIRE(glyph.quad.height == static_cast<float>(glyph.atlas.region.height));
        REQUIRE(glyph.quad.x == static_cast<float>(static_cast<int>(glyph.quad.x)));
    }
    INFO("the atlas page holds coverage where H was drawn");
    const Fenrir::ImageData& page = *test.store.TryGet(run.page);
    const Fenrir::ImageRegion h = run.glyphs[0].atlas.region;
    bool covered = false;
    for (std::uint32_t y = h.y; y < h.y + h.height; ++y)
    {
        for (std::uint32_t x = h.x; x < h.x + h.width; ++x)
        {
            covered = covered || page.pixels[std::size_t{y} * page.width + x] != std::byte{0};
        }
    }
    REQUIRE(covered);
}
