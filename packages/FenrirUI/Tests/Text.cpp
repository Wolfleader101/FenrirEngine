// Text on elements (ticket 09): a label's paragraph laid out at its style through the headless App (the spec's seam 1),
// and its glyphs in the root's draw list through a capturing target (seam 2). Paragraphs are laid out in target pixels,
// `size * scale * pixelsPerUnit`, and measured back into UI units; the roots here use the content-scale rule, so a
// target at content scale 2 lays text out at twice its UI size.
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "UITestApp.hpp"

namespace
{
    constexpr const char* LongText = "the quick brown fox jumps over the lazy dog and runs far away";

    // A root at `contentScale` over a capture target of 800 by 600 logical units, without pixel snapping when `snap` is
    // false, so rectangles keep the paragraph's fractional sizes.
    struct TextScene
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root;

        explicit TextScene(float contentScale = 1.0f, bool snap = true) : root(MakeRoot(contentScale, snap))
        {
        }

        Fenrir::UIRoot& MakeRoot(float contentScale, bool snap)
        {
            target = std::make_shared<UITest::CaptureTarget>(Fenrir::UITargetSize{{800.0f, 600.0f}, contentScale});
            Fenrir::UIRootConfig config;
            config.scaleRule = Fenrir::ScaleRule::ContentScale;
            config.snapToPixels = snap;
            config.target = target;
            return test.Roots().CreateRoot(config);
        }

        // A label under `parent` with `text` and the inline style `style` (declarations separated by semicolons).
        Fenrir::ElementId Label(Fenrir::ElementId parent, const std::string& text, const std::string& style = "")
        {
            const Fenrir::ElementId label = root.Create(parent, "label");
            root.SetText(label, text);
            SetStyle(label, style);
            return label;
        }

        void SetStyle(Fenrir::ElementId element, const std::string& style)
        {
            std::size_t start = 0;
            while (start < style.size())
            {
                const std::size_t end = std::min(style.find(';', start), style.size());
                const std::string declaration = style.substr(start, end - start);
                const std::size_t colon = declaration.find(':');
                if (colon != std::string::npos)
                {
                    std::string name = declaration.substr(0, colon);
                    name.erase(0, name.find_first_not_of(' '));
                    INFO("the inline declaration " << name << " is set");
                    REQUIRE(root.SetStyleProperty(element, name, declaration.substr(colon + 1)));
                }
                start = end + 1;
            }
        }

        const Fenrir::Paragraph& ParagraphOf(Fenrir::ElementId element) const
        {
            const Fenrir::Paragraph* paragraph = root.GetParagraph(element);
            REQUIRE(paragraph != nullptr);
            return *paragraph;
        }
    };

    std::vector<Fenrir::PainterVertex> Vertices(const Fenrir::DrawList2D& list)
    {
        std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
        return vertices;
    }

    // The first vertex of each glyph quad, in the order drawn.
    std::vector<std::vector<Fenrir::PainterVertex>> GlyphQuads(const Fenrir::DrawList2D& list)
    {
        const std::vector<Fenrir::PainterVertex> vertices = Vertices(list);
        std::vector<std::vector<Fenrir::PainterVertex>> quads;
        for (std::size_t i = 0; i + 3 < vertices.size(); i += 4)
        {
            if (static_cast<Fenrir::ShapeKind>(vertices[i].kinds[0]) == Fenrir::ShapeKind::Glyph)
            {
                quads.emplace_back(vertices.begin() + static_cast<std::ptrdiff_t>(i),
                                   vertices.begin() + static_cast<std::ptrdiff_t>(i + 4));
            }
        }
        return quads;
    }

    std::size_t GlyphCount(const Fenrir::Paragraph& paragraph)
    {
        std::size_t count = 0;
        for (const Fenrir::GlyphRun& run : paragraph.Runs())
        {
            count += run.glyphs.size();
        }
        return count;
    }

    constexpr std::uint32_t Rgba8(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
    {
        return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
} // namespace

TEST_CASE("A label's laid-out size is its paragraph's, laid out in target pixels and measured back in UI units",
          "[ui][text]")
{
    TextScene scene(2.0f, false);
    scene.SetStyle(scene.root.GetRootElement(), "align-items: flex-start");
    const Fenrir::ElementId label = scene.Label(scene.root.GetRootElement(), "Hello, labels", "font-size: 20px");
    scene.test.Frame();

    const Fenrir::Paragraph& paragraph = scene.ParagraphOf(label);
    REQUIRE(scene.root.GetText(label) == "Hello, labels");
    REQUIRE(paragraph.Text() == "Hello, labels");
    INFO("the paragraph is laid out at size * scale * pixelsPerUnit: 20 * 1 * 2");
    REQUIRE_FALSE(paragraph.Runs().empty());
    REQUIRE(UITest::Near(paragraph.Runs().front().size, 40.0f));
    const Fenrir::Rect2D content = scene.root.GetBoxes(label).content;
    INFO("the content box is the paragraph's size divided back by 2");
    REQUIRE(UITest::Near(content.width, paragraph.Size().x / 2.0f));
    REQUIRE(UITest::Near(content.height, paragraph.Size().y / 2.0f));
    REQUIRE(content.width > 0.0f);
}

TEST_CASE("A label wraps at its parent's width, and white-space: nowrap keeps it on one line", "[ui][text]")
{
    TextScene scene;
    const Fenrir::ElementId panel = scene.root.Create(scene.root.GetRootElement(), "panel");
    scene.SetStyle(panel, "width: 120px");
    const Fenrir::ElementId wrapped = scene.Label(panel, LongText);
    const Fenrir::ElementId unwrapped = scene.Label(panel, LongText, "white-space: nowrap");
    scene.test.Frame();

    INFO("the wrapping label breaks into lines no wider than the panel");
    const Fenrir::Paragraph& lines = scene.ParagraphOf(wrapped);
    REQUIRE(lines.Lines().size() > 1);
    REQUIRE(lines.Size().x <= 120.0f);
    REQUIRE(UITest::Near(scene.root.GetBoxes(wrapped).content.width, 120.0f));
    REQUIRE(scene.root.GetBoxes(wrapped).content.height > scene.root.GetBoxes(unwrapped).content.height);

    INFO("the nowrap label is one line, past the panel's width");
    const Fenrir::Paragraph& line = scene.ParagraphOf(unwrapped);
    REQUIRE(line.Lines().size() == 1);
    REQUIRE(line.Size().x > 120.0f);
}

TEST_CASE("text-overflow: ellipsis cuts a nowrap label at its fixed width", "[ui][text]")
{
    TextScene scene;
    scene.SetStyle(scene.root.GetRootElement(), "align-items: flex-start");
    const Fenrir::ElementId clipped =
        scene.Label(scene.root.GetRootElement(), LongText, "width: 100px; white-space: nowrap");
    const Fenrir::ElementId ellipsised = scene.Label(scene.root.GetRootElement(), LongText,
                                                     "width: 100px; white-space: nowrap; text-overflow: ellipsis");
    scene.test.Frame();

    REQUIRE(scene.root.GetBoxes(ellipsised).content.width == 100.0f);
    INFO("clip lets the line run past the width; ellipsis ends it inside the width with fewer glyphs");
    const Fenrir::Paragraph& clip = scene.ParagraphOf(clipped);
    const Fenrir::Paragraph& ellipsis = scene.ParagraphOf(ellipsised);
    REQUIRE(clip.Size().x > 100.0f);
    REQUIRE(ellipsis.Lines().size() == 1);
    REQUIRE(ellipsis.Size().x <= 100.0f);
    REQUIRE(GlyphCount(ellipsis) < GlyphCount(clip));
}

TEST_CASE("text-align moves a label's lines across its width", "[ui][text]")
{
    TextScene scene;
    const Fenrir::ElementId left = scene.Label(scene.root.GetRootElement(), "Hi", "width: 300px; text-align: left");
    const Fenrir::ElementId centre = scene.Label(scene.root.GetRootElement(), "Hi", "width: 300px; text-align: center");
    const Fenrir::ElementId right = scene.Label(scene.root.GetRootElement(), "Hi", "width: 300px; text-align: right");
    scene.test.Frame();

    const float leftStart = scene.ParagraphOf(left).Lines().front().left;
    const float centreStart = scene.ParagraphOf(centre).Lines().front().left;
    const float rightStart = scene.ParagraphOf(right).Lines().front().left;
    const float width = scene.ParagraphOf(left).Lines().front().width;
    REQUIRE(UITest::Near(leftStart, 0.0f));
    REQUIRE(UITest::Near(centreStart, (300.0f - width) / 2.0f, 0.5f));
    REQUIRE(UITest::Near(rightStart, 300.0f - width, 0.5f));

    INFO("the drawn glyphs move with the lines");
    const auto quads = GlyphQuads(scene.target->lists.back());
    REQUIRE(quads.size() == 6);
    REQUIRE(quads[2][0].position.x > quads[0][0].position.x + 100.0f);
    REQUIRE(quads[4][0].position.x > quads[2][0].position.x + 100.0f);
}

TEST_CASE("font-size inherits from a parent and a class's own wins", "[ui][text]")
{
    TextScene scene;
    scene.root.AddStylesheet(".parent { font-size: 30px; } .big { font-size: 40px; }", "sizes");
    const Fenrir::ElementId parent = scene.root.Create(scene.root.GetRootElement(), "panel");
    scene.root.AddClass(parent, "parent");
    const Fenrir::ElementId inherits = scene.Label(parent, "Same");
    const Fenrir::ElementId overrides = scene.Label(parent, "Bigger");
    scene.root.AddClass(overrides, "big");
    scene.test.Frame();

    REQUIRE(scene.root.GetResolvedStyle(inherits)->fontSize == 30.0f);
    REQUIRE(scene.root.GetResolvedStyle(overrides)->fontSize == 40.0f);
    REQUIRE(UITest::Near(scene.ParagraphOf(inherits).Runs().front().size, 30.0f));
    REQUIRE(UITest::Near(scene.ParagraphOf(overrides).Runs().front().size, 40.0f));

    INFO("a change to the parent's size reaches the label that inherits it, and lays it out again");
    const float before = scene.root.GetBoxes(inherits).content.height;
    scene.root.SetStyleProperty(parent, "font-size", "60px");
    scene.test.Frame();
    REQUIRE(scene.root.GetResolvedStyle(inherits)->fontSize == 60.0f);
    REQUIRE(scene.root.GetResolvedStyle(overrides)->fontSize == 40.0f);
    REQUIRE(scene.root.GetBoxes(inherits).content.height > before);
}

TEST_CASE("Labels in a flex row with flex-grow wrap at their share of it", "[ui][text]")
{
    TextScene scene;
    const Fenrir::ElementId row = scene.root.Create(scene.root.GetRootElement(), "panel");
    scene.SetStyle(row, "flex-direction: row; width: 300px");
    const Fenrir::ElementId first = scene.Label(row, LongText, "flex-grow: 1; flex-basis: 0");
    const Fenrir::ElementId second = scene.Label(row, LongText, "flex-grow: 1; flex-basis: 0");
    scene.test.Frame();

    REQUIRE(UITest::Near(scene.root.GetBoxes(first).content.width, 150.0f));
    REQUIRE(UITest::Near(scene.root.GetBoxes(second).content.width, 150.0f));
    const Fenrir::Paragraph& paragraph = scene.ParagraphOf(first);
    REQUIRE(paragraph.Lines().size() > 1);
    REQUIRE(paragraph.Size().x <= 150.0f);
    INFO("the row is as tall as the wrapped lines");
    REQUIRE(UITest::Near(scene.root.GetBoxes(row).border.height, scene.root.GetBoxes(first).border.height));
}

TEST_CASE("Text properties read from inline style and every one inherits", "[ui][text]")
{
    TextScene scene;
    const Fenrir::ElementId parent = scene.root.Create(scene.root.GetRootElement(), "panel");
    scene.SetStyle(parent, "color: #ff0000; font-family: 'Body Text', sans-serif; font-size: 22px; "
                           "font-weight: bold; font-style: italic; text-align: center; line-height: 1.5; "
                           "letter-spacing: 2px; text-shadow: 1px 2px 3px blue, -1px 0 green; white-space: nowrap; "
                           "text-overflow: ellipsis");
    const Fenrir::ElementId label = scene.Label(parent, "Styled");
    scene.test.Frame();

    const Fenrir::ResolvedStyle& style = *scene.root.GetResolvedStyle(label);
    REQUIRE(style.color.r == 1.0f);
    REQUIRE(style.color.g == 0.0f);
    REQUIRE(style.fontFamily == "Body Text");
    REQUIRE(style.fontSize == 22.0f);
    REQUIRE(style.fontWeight == Fenrir::FontWeightBold);
    REQUIRE(style.fontStyle == Fenrir::FontStyle::Italic);
    REQUIRE(style.textAlign == Fenrir::TextAlign::Center);
    REQUIRE(style.lineHeight == Fenrir::Length::Percent(150.0f));
    REQUIRE(style.letterSpacing == 2.0f);
    REQUIRE(style.textShadow.size() == 2);
    REQUIRE(style.textShadow[0].offset == Fenrir::Math::Vec2(1.0f, 2.0f));
    REQUIRE(style.textShadow[0].blur == 3.0f);
    REQUIRE(style.textShadow[1].offset == Fenrir::Math::Vec2(-1.0f, 0.0f));
    REQUIRE(style.whiteSpace == Fenrir::WhiteSpace::NoWrap);
    REQUIRE(style.textOverflow == Fenrir::TextOverflow::Ellipsis);

    INFO("the paragraph takes them: weight, style, letter spacing and the paragraph style");
    const Fenrir::Paragraph& paragraph = scene.ParagraphOf(label);
    REQUIRE(paragraph.Style().align == Fenrir::TextAlign::Center);
    REQUIRE(paragraph.Style().lineHeight == 1.5f);
    REQUIRE(paragraph.Style().whiteSpace == Fenrir::WhiteSpace::NoWrap);
    REQUIRE(paragraph.Style().ellipsis);

    INFO("text-shadow takes no spread, and pre is not a white-space FenrirUI takes");
    REQUIRE_FALSE(scene.root.SetStyleProperty(label, "text-shadow", "1px 1px 2px 3px black"));
    REQUIRE_FALSE(scene.root.SetStyleProperty(label, "white-space", "pre"));
}

TEST_CASE("font-family resolves through the app's font library, with families registered from code", "[ui][text]")
{
    TextScene scene;
    Fenrir::FontLibrary* fonts = scene.test.Roots().GetFonts();
    REQUIRE(fonts != nullptr);
    REQUIRE(fonts == scene.test.app.TryGetResource<Fenrir::FontLibrary>());
    const std::filesystem::path file =
        std::filesystem::path(FENRIR_TEXT_HARFBUZZ_TEST) / "subset" / "data" / "fonts" / "Roboto-Regular.ttf";
    const Fenrir::AssetHandle<Fenrir::Font> roboto =
        scene.test.assets.Store().Load<Fenrir::Font>(file.string(), Fenrir::AssetOwner::App()).handle;
    REQUIRE_FALSE(roboto.IsEmpty());
    fonts->AddFace("Body", roboto);

    const Fenrir::ElementId body = scene.Label(scene.root.GetRootElement(), "Body", "font-family: Body");
    const Fenrir::ElementId plain = scene.Label(scene.root.GetRootElement(), "Plain");
    scene.test.Frame();

    REQUIRE(scene.ParagraphOf(body).Runs().front().font == roboto);
    REQUIRE(scene.ParagraphOf(plain).Runs().front().font == fonts->DefaultFont());
}

TEST_CASE("Text set again, or on any element, lays out again", "[ui][text]")
{
    TextScene scene;
    scene.SetStyle(scene.root.GetRootElement(), "align-items: flex-start");
    const Fenrir::ElementId button = scene.root.Create(scene.root.GetRootElement(), "button");
    REQUIRE(scene.root.GetParagraph(button) == nullptr);
    scene.root.SetText(button, "OK");
    scene.test.Frame();
    const float shortWidth = scene.root.GetBoxes(button).content.width;
    REQUIRE(shortWidth > 0.0f);

    scene.root.SetText(button, "Cancel everything");
    scene.test.Frame();
    REQUIRE(scene.root.GetBoxes(button).content.width > shortWidth);
    REQUIRE(scene.root.GetText(button) == "Cancel everything");
}

TEST_CASE("A label's glyph quads sit inside its content box", "[ui][text]")
{
    TextScene scene(2.0f);
    scene.SetStyle(scene.root.GetRootElement(), "align-items: flex-start");
    const Fenrir::ElementId panel = scene.root.Create(scene.root.GetRootElement(), "panel");
    scene.SetStyle(panel, "margin-left: 40px; margin-top: 30px; padding: 10px; border-width: 3px");
    const Fenrir::ElementId label = scene.Label(panel, "Glyphs in a box", "padding: 5px; font-size: 18px");
    scene.test.Frame();

    const Fenrir::Rect2D content = scene.root.GetBoxes(label).content;
    const auto quads = GlyphQuads(scene.target->lists.back());
    REQUIRE(quads.size() == GlyphCount(scene.ParagraphOf(label)));
    REQUIRE_FALSE(quads.empty());
    for (const auto& quad : quads)
    {
        for (const Fenrir::PainterVertex& vertex : quad)
        {
            // Logical units, which are UI units under the content-scale rule; glyph bitmaps reach half a pixel out.
            REQUIRE(vertex.position.x >= content.x - 0.5f);
            REQUIRE(vertex.position.y >= content.y - 0.5f);
            REQUIRE(vertex.position.x <= content.x + content.width + 0.5f);
            REQUIRE(vertex.position.y <= content.y + content.height + 0.5f);
        }
    }
}

TEST_CASE("A label's text-shadow draws before its text, offset and in its colour", "[ui][text]")
{
    TextScene scene;
    const Fenrir::ElementId label =
        scene.Label(scene.root.GetRootElement(), "Shade", "color: #0000ff; text-shadow: 2px 3px #ff0000");
    scene.test.Frame();

    const std::size_t glyphs = GlyphCount(scene.ParagraphOf(label));
    const auto quads = GlyphQuads(scene.target->lists.back());
    REQUIRE(quads.size() == glyphs * 2);
    INFO("the shadow's quads come first, red and moved by (2, 3); then the text's, blue");
    REQUIRE(quads[0][0].colour == Rgba8(255, 0, 0, 255));
    REQUIRE(quads[glyphs][0].colour == Rgba8(0, 0, 255, 255));
    REQUIRE(UITest::Near(quads[0][0].position.x, quads[glyphs][0].position.x + 2.0f));
    REQUIRE(UITest::Near(quads[0][0].position.y, quads[glyphs][0].position.y + 3.0f));
}
