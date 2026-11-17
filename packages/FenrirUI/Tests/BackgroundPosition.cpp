// `background-position` through a capturing target (the spec's "The remaining longhands"): the value read and refused,
// a `cover` and a `contain` background image placed by it, the `background` shorthand's position, and its transition.
// The expected places are worked from CSS: a percentage aligns that point of the image with that point of the box, so
// an image's offset is (box - image) times it; a length offsets the image's top-left from the box's.
#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/PainterVertex.hpp"
#include "UITestApp.hpp"

namespace
{
    using Fenrir::ElementId;
    using Fenrir::Length;
    using UITest::Near;

    // A root 400 by 400 holding one 200 by 200 panel at its top-left, which the default stylesheet leaves unpainted,
    // and two white images for its url()s: builtin:wide, 100 by 50, and builtin:tall, 50 by 100.
    struct PositionTest
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {400.0f, 400.0f});
        ElementId panel = root.Create(root.GetRootElement(), "panel");

        PositionTest()
        {
            UITest::AddWhiteImage(test.app, "builtin:wide", 100, 50);
            UITest::AddWhiteImage(test.app, "builtin:tall", 50, 100);
            INFO("the panel takes its size");
            REQUIRE(root.SetStyleProperty(panel, "width", "200px"));
            REQUIRE(root.SetStyleProperty(panel, "height", "200px"));
        }

        // Gives the panel each declaration, then draws a frame.
        void Style(std::initializer_list<std::pair<std::string_view, std::string_view>> declarations)
        {
            for (const auto& [name, value] : declarations)
            {
                INFO("the panel takes " << name << ": " << value);
                REQUIRE(root.SetStyleProperty(panel, name, value));
            }
            test.Frame();
        }

        // The latest frame's one drawn quad, all four of its vertices.
        std::vector<Fenrir::PainterVertex> Quad() const
        {
            const std::vector<Fenrir::PainterVertex> vertices = UITest::Vertices(target->lists.back());
            INFO("the panel draws its image as one quad and nothing else: " << vertices.size() << " vertices");
            REQUIRE(vertices.size() == 4);
            return vertices;
        }

        // The box the latest frame's one quad draws: its shape's centre and half size, without the quad's margin.
        Fenrir::Rect2D Drawn() const
        {
            return UITest::QuadBox(Quad(), 0);
        }

        const Fenrir::ResolvedStyle& Resolved() const
        {
            const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(panel);
            INFO("the panel is alive");
            REQUIRE(style != nullptr);
            return *style;
        }
    };
} // namespace

TEST_CASE("background-position reads one or two keywords, lengths or percentages as CSS places them", "[ui][style]")
{
    struct Case
    {
        std::string_view value;
        Length x;
        Length y;
    };
    const Case cases[] = {
        {"center", Length::Percent(50.0f), Length::Percent(50.0f)},
        {"left", Length::Percent(0.0f), Length::Percent(50.0f)},
        {"top", Length::Percent(50.0f), Length::Percent(0.0f)},
        {"right bottom", Length::Percent(100.0f), Length::Percent(100.0f)},
        {"bottom right", Length::Percent(100.0f), Length::Percent(100.0f)},
        {"center left", Length::Percent(0.0f), Length::Percent(50.0f)},
        {"10px 20%", Length::Px(10.0f), Length::Percent(20.0f)},
        {"left 15px", Length::Percent(0.0f), Length::Px(15.0f)},
        {"25%", Length::Percent(25.0f), Length::Percent(50.0f)},
    };
    for (const Case& c : cases)
    {
        PositionTest t;
        t.Style({{"background-position", c.value}});
        INFO("'" << c.value << "' places the image's x at " << t.Resolved().backgroundPositionX.value << " and y at "
                 << t.Resolved().backgroundPositionY.value);
        REQUIRE(t.Resolved().backgroundPositionX == c.x);
        REQUIRE(t.Resolved().backgroundPositionY == c.y);
    }
}

TEST_CASE("background-position is 0% 0% when nothing gives it, as CSS's initial value", "[ui][style]")
{
    PositionTest t;
    t.test.Frame();
    INFO("an unset position is " << t.Resolved().backgroundPositionX.value << " "
                                 << t.Resolved().backgroundPositionY.value << ", not 0% 0%");
    REQUIRE(t.Resolved().backgroundPositionX == Length::Percent(0.0f));
    REQUIRE(t.Resolved().backgroundPositionY == Length::Percent(0.0f));
}

TEST_CASE("background-position refuses what CSS does not read as a position", "[ui][style]")
{
    PositionTest t;
    for (const std::string_view refused :
         {"left right", "top bottom", "top 10px", "10px left", "1px 2px 3px", "auto", "middle"})
    {
        INFO("'" << refused << "' is not a position and is refused");
        REQUIRE_FALSE(t.root.SetStyleProperty(t.panel, "background-position", refused));
    }
}

TEST_CASE("SetBackgroundPosition gives both lengths", "[ui][style]")
{
    PositionTest t;
    t.root.EditInlineStyle(t.panel)->SetBackgroundPosition(Length::Px(5.0f), Length::Percent(75.0f));
    t.test.Frame();
    INFO("the typed setter gives 5px across and 75% down");
    REQUIRE(t.Resolved().backgroundPositionX == Length::Px(5.0f));
    REQUIRE(t.Resolved().backgroundPositionY == Length::Percent(75.0f));
}

TEST_CASE("A contained background image sits where background-position places it in the box", "[ui][paint]")
{
    // A 100 by 50 image contained in a 200 by 200 box is scaled to 200 by 100, which leaves 100 down to place it in.
    struct Case
    {
        std::string_view position;
        float y;
    };
    for (const Case& c : {Case{"center", 50.0f}, Case{"top", 0.0f}, Case{"bottom", 100.0f}, Case{"left 25%", 25.0f},
                          Case{"0 30px", 30.0f}})
    {
        PositionTest t;
        t.Style({{"background-image", "url(builtin:wide)"},
                 {"background-size", "contain"},
                 {"background-position", c.position}});
        const Fenrir::Rect2D drawn = t.Drawn();
        INFO("at '" << c.position << "' the 200 by 100 image draws at " << drawn.x << ", " << drawn.y << " size "
                    << drawn.width << " by " << drawn.height << ", not at 0, " << c.y);
        REQUIRE(Near(drawn, {0.0f, c.y, 200.0f, 100.0f}, 0.01f));
    }
}

TEST_CASE("A covering background image placed right crops its left", "[ui][paint]")
{
    // A 100 by 50 image covering a 200 by 200 box is scaled to 400 by 200: placed right, the box shows its right half.
    PositionTest t;
    t.Style(
        {{"background-image", "url(builtin:wide)"}, {"background-size", "cover"}, {"background-position", "right"}});
    float least = 1.0f;
    float most = 0.0f;
    for (const Fenrir::PainterVertex& vertex : t.Quad())
    {
        least = std::min(least, vertex.uv.x);
        most = std::max(most, vertex.uv.x);
    }
    INFO("the box shows u from " << least << " to " << most << ", not the image's right half, 0.5 to 1, past them "
                                 << "only by the quad's one-unit margin");
    REQUIRE(Near(least, 0.5f, 0.01f));
    REQUIRE(Near(most, 1.0f, 0.01f));
}

TEST_CASE("A stretched background image ignores background-position", "[ui][paint]")
{
    PositionTest stretched;
    stretched.Style({{"background-image", "url(builtin:wide)"}, {"background-position", "right bottom"}});
    float least = 1.0f;
    for (const Fenrir::PainterVertex& vertex : stretched.Quad())
    {
        least = std::min(least, vertex.uv.x);
    }
    INFO("a stretched image fills the box from u 0 (less the margin), not " << least);
    REQUIRE(Near(least, 0.0f, 0.01f));
}

TEST_CASE("A gradient ignores background-position", "[ui][paint]")
{
    PositionTest plain;
    plain.Style({{"background-image", "linear-gradient(90deg, #000000, #ffffff)"}});
    PositionTest placed;
    placed.Style({{"background-image", "linear-gradient(90deg, #000000, #ffffff)"}, {"background-position", "right"}});
    const Fenrir::DrawList2D& a = plain.target->lists.back();
    const Fenrir::DrawList2D& b = placed.target->lists.back();
    INFO("a gradient placed right draws the same vertices as one not placed");
    REQUIRE(a.vertices == b.vertices);
}

TEST_CASE("The background shorthand's position places its image as the longhand does", "[ui][paint]")
{
    // A 50 by 100 image contained in a 200 by 200 box is scaled to 100 by 200, which leaves 100 across: right is 100.
    PositionTest shorthand;
    shorthand.Style({{"background", "url(builtin:tall) right / contain"}});
    PositionTest longhands;
    longhands.Style(
        {{"background-image", "url(builtin:tall)"}, {"background-size", "contain"}, {"background-position", "right"}});
    const Fenrir::Rect2D a = shorthand.Drawn();
    const Fenrir::Rect2D b = longhands.Drawn();
    INFO("the shorthand draws its image at " << a.x << ", " << a.y << " and the longhands at " << b.x << ", " << b.y);
    REQUIRE(Near(a, {100.0f, 0.0f, 100.0f, 200.0f}, 0.01f));
    REQUIRE(Near(b, a, 0.01f));
}

TEST_CASE("background-position transitions as two lengths", "[ui][motion]")
{
    PositionTest t;
    t.root.AddStylesheet(".p { background-position: 0% 10px; transition: background-position 1s linear; }"
                         ".p.moved { background-position: 100% 30px; }",
                         "sheet");
    t.root.AddClass(t.panel, "p");
    t.test.Frames(1, 0.125);
    t.root.AddClass(t.panel, "moved");
    t.test.Frames(1, 0.125);
    t.test.Frames(4, 0.125);
    const Fenrir::ResolvedStyle& style = t.Resolved();
    INFO("half way through a linear second from 0% 10px to 100% 30px it shows "
         << style.backgroundPositionX.value << " and " << style.backgroundPositionY.value << ", not 50% 20px");
    REQUIRE(style.backgroundPositionX.unit == Fenrir::LengthUnit::Percent);
    REQUIRE(Near(style.backgroundPositionX.value, 50.0f));
    REQUIRE(style.backgroundPositionY.unit == Fenrir::LengthUnit::Px);
    REQUIRE(Near(style.backgroundPositionY.value, 20.0f));
}
