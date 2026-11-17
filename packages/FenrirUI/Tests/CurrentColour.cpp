// `currentcolor` through a headless App (the spec's seam 1): what an element's resolved style holds, and its border as
// drawn, ticking fixed frame deltas of an eighth of a second.
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "UITestApp.hpp"

namespace
{
    using Fenrir::Colour;
    using Fenrir::ElementId;
    using UITest::Near;
    using UITest::StyleTest;

    constexpr double eighth = 0.125;

    // Red and blue as the style holds them: linear, opaque.
    constexpr Colour red{1.0f, 0.0f, 0.0f, 1.0f};
    constexpr Colour blue{0.0f, 0.0f, 1.0f, 1.0f};
    // Half way from red to blue: sRGB (0.5, 0, 0.5), linear 0.21404, as CSS mixes in sRGB.
    constexpr Colour halfway{0.21404f, 0.0f, 0.21404f, 1.0f};
} // namespace

TEST_CASE("currentcolor is a colour in any case", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; background-color: CurrentColor; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    INFO("background-color: CurrentColor reads, as the element's red");
    REQUIRE(Near(t.Style(box).backgroundColor, red));
}

TEST_CASE("color: currentcolor takes the parent's colour, as inherit does", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".parent { color: blue; } .child { color: red; } .child.own { color: CURRENTCOLOR; }",
                         "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child own");
    t.test.Frame();
    INFO("the later color: currentcolor is the parent's blue, not the earlier red");
    REQUIRE(Near(t.Style(child).color, blue));
}

TEST_CASE("A border shorthand with no colour gives every side the text colour", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; border: 2px solid; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = t.Style(box);
    INFO("every side is the text colour, red");
    REQUIRE(Near(style.borderTopColor, red));
    REQUIRE(Near(style.borderRightColor, red));
    REQUIRE(Near(style.borderBottomColor, red));
    REQUIRE(Near(style.borderLeftColor, red));
}

TEST_CASE("A border with no colour draws in the text colour", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { width: 40px; height: 20px; color: red; border: 2px solid; }", "sheet");
    t.Make("box");
    t.test.Frame();
    const std::vector<Fenrir::PainterVertex> quads = UITest::Quads(t.target->lists.back());
    INFO("one quad, the border's box (the transparent background draws nothing), its border red");
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].borderColours[0] == UITest::Rgba8(255, 0, 0, 255));
}

TEST_CASE("A box shadow with no colour is the text colour", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; box-shadow: 0 2px 4px; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    INFO("one shadow, red");
    REQUIRE(t.Style(box).boxShadow.size() == 1);
    REQUIRE(Near(t.Style(box).boxShadow[0].colour, red));
}

TEST_CASE("A border colour no rule gives is the element's own text colour", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".parent { color: red; border-top-width: 1px; } .child { color: blue; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child");
    t.test.Frame();
    INFO("the initial border colour is currentcolor: red on the parent, blue on its child");
    REQUIRE(Near(t.Style(parent).borderTopColor, red));
    REQUIRE(Near(t.Style(child).borderTopColor, blue));
}

TEST_CASE("border-color: currentcolor on a child is the child's colour, not its parent's", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".parent { color: red; border-color: currentcolor; } .child { color: blue; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child");
    t.Set(child, "border-color", "currentcolor");
    t.test.Frame();
    INFO("each resolves currentcolor to its own color");
    REQUIRE(Near(t.Style(parent).borderLeftColor, red));
    REQUIRE(Near(t.Style(child).borderLeftColor, blue));
}

TEST_CASE("currentcolor is a colour in every colour-taking property", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; overflow: scroll; background-color: currentcolor;"
                         " background-image: linear-gradient(currentcolor, blue);"
                         " text-shadow: 1px 1px; filter: drop-shadow(1px 1px);"
                         " scrollbar-color: currentcolor blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = t.Style(box);
    INFO("background-color: currentcolor is red");
    REQUIRE(Near(style.backgroundColor, red));
    const auto* gradient = std::get_if<Fenrir::StyleLinearGradient>(&style.backgroundImage);
    INFO("a gradient stop of currentcolor is red");
    REQUIRE(gradient != nullptr);
    REQUIRE(gradient->stops.size() == 2);
    REQUIRE(Near(gradient->stops[0].colour, red));
    INFO("a text shadow's and a drop shadow's colour left out are red");
    REQUIRE(style.textShadow.size() == 1);
    REQUIRE(Near(style.textShadow[0].colour, red));
    REQUIRE(style.filter.size() == 1);
    REQUIRE(Near(style.filter[0].colour, red));
    INFO("scrollbar-color's currentcolor is the scroll view's colour");
    REQUIRE(Near(style.scrollbarColor.thumb, red));
    REQUIRE(Near(style.scrollbarColor.track, blue));
}

TEST_CASE("A pseudo-element's currentcolor is its own colour", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; overflow: scroll; }"
                         " .box::scrollbar-thumb { color: blue; background-color: currentcolor; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    const Fenrir::ResolvedStyle* thumb = t.root.GetPseudoElementStyle(box, Fenrir::PseudoElement::ScrollbarThumb);
    INFO("the thumb's background is its own blue, not the scroll view's red");
    REQUIRE(thumb != nullptr);
    REQUIRE(Near(thumb->backgroundColor, blue));
}

TEST_CASE("A pseudo-element's currentcolor follows its element's color transition", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; overflow: scroll; transition: color 1s linear; }"
                         " .box.cool { color: blue; }"
                         " .box::scrollbar-thumb { background-color: currentcolor; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "cool");
    t.test.Frames(5, eighth);
    const Fenrir::ResolvedStyle* thumb = t.root.GetPseudoElementStyle(box, Fenrir::PseudoElement::ScrollbarThumb);
    INFO("half way through the element's color transition, the thumb it colours is half way too");
    REQUIRE(thumb != nullptr);
    REQUIRE(Near(thumb->backgroundColor, halfway));
}

TEST_CASE("An inherited text shadow's currentcolor is the inheriting element's own colour", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".parent { color: red; text-shadow: 1px 1px; } .child { color: blue; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child");
    t.test.Frame();
    INFO("the parent's shadow is red; its child inherits the shadow, in its own blue, as CSS keeps currentcolor");
    REQUIRE(Near(t.Style(parent).textShadow.at(0).colour, red));
    REQUIRE(Near(t.Style(child).textShadow.at(0).colour, blue));
}

TEST_CASE("While color transitions, a currentcolor border is the text's colour each frame", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; border: 2px solid; transition: color 1s linear; }"
                         " .box.cool { color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "cool");
    for (int frame = 1; frame <= 8; ++frame)
    {
        t.test.Frames(1, eighth);
        const Fenrir::ResolvedStyle& style = t.Style(box);
        INFO("frame " << frame << ": the border is the colour the text shows");
        REQUIRE(Near(style.borderTopColor, style.color));
        if (frame == 5)
        {
            INFO("half way, both are half way from red to blue");
            REQUIRE(Near(style.borderTopColor, halfway));
        }
    }
}

TEST_CASE("A transition from currentcolor to a colour mixes the resolved colours", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet(".box { color: red; border-top-width: 1px; transition: border-top-color 1s linear; }"
                         " .box.blue { border-top-color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "blue");
    t.test.Frames(5, eighth);
    INFO("half way from currentcolor, red, to blue");
    REQUIRE(Near(t.Style(box).borderTopColor, halfway));
}

TEST_CASE("A transition from currentcolor while color moves starts from the colour shown", "[ui][currentcolor]")
{
    StyleTest t;
    // The border's transition waits a second, showing where it starts from all that time.
    t.root.AddStylesheet(".box { color: red; border-top-width: 1px;"
                         " transition: color 1s linear, border-top-color 1s linear 1s; }"
                         " .box.cool { color: blue; } .box.cool.edge { border-top-color: lime; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "cool");
    t.test.Frames(5, eighth);
    t.root.AddClass(box, "edge");
    t.test.Frames(1, eighth);
    const Colour from = t.Style(box).borderTopColor;
    INFO("the border starts from the text's colour part way to blue, not from the old red");
    REQUIRE(from.r > 0.05f);
    REQUIRE(from.r < 0.5f);
    REQUIRE(from.b > 0.05f);
}

TEST_CASE("A keyframe animation from currentcolor mixes the colour it resolves to", "[ui][currentcolor]")
{
    StyleTest t;
    t.root.AddStylesheet("@keyframes cool { from { border-top-color: currentcolor; } to { border-top-color: blue; } }"
                         " .box { color: red; border-top-width: 1px; animation: cool 1s linear infinite; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(5, eighth);
    INFO("half way from currentcolor, red, to blue");
    REQUIRE(Near(t.Style(box).borderTopColor, halfway));
}

TEST_CASE("A currentcolor border with a border-color transition snaps with the text when color changes",
          "[ui][currentcolor]")
{
    StyleTest t;
    // The border's colour is currentcolor before and after: only color changes, and color has no transition.
    t.root.AddStylesheet(".box { color: red; border: 1px solid; transition: border-color 0.3s linear; }"
                         " .box.cool { color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    {
        INFO("the border did not start in the text's red");
        REQUIRE(Near(t.Style(box).borderTopColor, red));
    }
    t.root.AddClass(box, "cool");
    t.test.Frames(1, eighth);
    const Fenrir::ResolvedStyle& style = t.Style(box);
    {
        INFO("the text did not snap to blue on the frame its color changed");
        REQUIRE(Near(style.color, blue));
    }
    {
        INFO("a border side was not blue on that same frame: a border-color transition ran from red");
        REQUIRE(Near(style.borderTopColor, blue));
        REQUIRE(Near(style.borderRightColor, blue));
        REQUIRE(Near(style.borderBottomColor, blue));
        REQUIRE(Near(style.borderLeftColor, blue));
    }
    t.test.Frames(1, eighth);
    INFO("the border left blue a frame later: a border-color transition started after the snap");
    REQUIRE(Near(t.Style(box).borderTopColor, blue));
}
