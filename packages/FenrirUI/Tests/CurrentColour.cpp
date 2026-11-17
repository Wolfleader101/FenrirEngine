// `currentcolor` and the `inset` and `border-image` shorthands through a headless App (the spec's seam 1): what an
// element's resolved style holds, and its border as drawn, ticking fixed frame deltas of an eighth of a second.
#include <algorithm>
#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "UITestApp.hpp"

namespace
{
    using Fenrir::Colour;
    using Fenrir::ElementId;
    using UITest::Near;

    constexpr double eighth = 0.125;

    // Red and blue as the style holds them: linear, opaque.
    constexpr Colour red{1.0f, 0.0f, 0.0f, 1.0f};
    constexpr Colour blue{0.0f, 0.0f, 1.0f, 1.0f};

    bool Same(const Colour& a, const Colour& b)
    {
        return Near(a.r, b.r) && Near(a.g, b.g) && Near(a.b, b.b) && Near(a.a, b.a);
    }

    struct ColourTest
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

        // A panel under `parent` with the classes `classes`, space-separated.
        ElementId Make(ElementId parent, std::string_view classes)
        {
            const ElementId element = root.Create(parent, "panel");
            for (std::size_t start = 0; start < classes.size();)
            {
                const std::size_t end = std::min(classes.find(' ', start), classes.size());
                root.AddClass(element, classes.substr(start, end - start));
                start = end + 1;
            }
            return element;
        }

        ElementId Make(std::string_view classes)
        {
            return Make(root.GetRootElement(), classes);
        }

        const Fenrir::ResolvedStyle& Style(ElementId element) const
        {
            const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
            INFO("the element is alive and styled");
            REQUIRE(style != nullptr);
            return *style;
        }
    };
} // namespace

TEST_CASE("currentcolor parses in any case, and color: currentcolor takes the parent's colour", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".parent { color: blue; } .child { color: red; } .child.own { color: CURRENTCOLOR; }",
                         "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child own");
    t.test.Frame();
    INFO("CURRENTCOLOR is a colour, and as color's value it is inherit: the parent's blue over the earlier red");
    REQUIRE(Same(t.Style(child).color, blue));
}

TEST_CASE("A border's and a box shadow's colour left out is the text colour", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { width: 40px; height: 20px; color: red; border: 2px solid; box-shadow: 0 2px 4px; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    INFO("the border shorthand with no colour gives every side the text colour, red");
    const Fenrir::ResolvedStyle& style = t.Style(box);
    REQUIRE(Same(style.borderTopColor, red));
    REQUIRE(Same(style.borderRightColor, red));
    REQUIRE(Same(style.borderBottomColor, red));
    REQUIRE(Same(style.borderLeftColor, red));
    INFO("a box shadow with no colour is red too");
    REQUIRE(style.boxShadow.size() == 1);
    REQUIRE(Same(style.boxShadow[0].colour, red));
    INFO("the border draws in red: the shadow, then the border's box (the transparent background draws nothing)");
    const std::vector<Fenrir::PainterVertex> quads = UITest::Quads(t.target->lists.back());
    REQUIRE(quads.size() == 2);
    REQUIRE(quads[1].borderColours[0] == UITest::Rgba8(255, 0, 0, 255));
}

TEST_CASE("A border colour no rule gives is the element's own text colour", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".parent { color: red; border-top-width: 1px; } .child { color: blue; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child");
    t.test.Frame();
    INFO("the initial border colour is currentcolor: red on the parent, blue on its child");
    REQUIRE(Same(t.Style(parent).borderTopColor, red));
    REQUIRE(Same(t.Style(child).borderTopColor, blue));
}

TEST_CASE("border-color: currentcolor on a child is the child's colour, not its parent's", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".parent { color: red; border-color: currentcolor; } .child { color: blue; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child");
    REQUIRE(t.root.SetStyleProperty(child, "border-color", "currentcolor"));
    t.test.Frame();
    INFO("each resolves currentcolor to its own color");
    REQUIRE(Same(t.Style(parent).borderLeftColor, red));
    REQUIRE(Same(t.Style(child).borderLeftColor, blue));
}

TEST_CASE("currentcolor is a colour in every colour-taking property", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; overflow: scroll; background-color: currentcolor;"
                         " background-image: linear-gradient(currentcolor, blue);"
                         " text-shadow: 1px 1px; filter: drop-shadow(1px 1px);"
                         " scrollbar-color: currentcolor blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = t.Style(box);
    INFO("background-color: currentcolor is red");
    REQUIRE(Same(style.backgroundColor, red));
    INFO("a gradient stop of currentcolor is red");
    const auto* gradient = std::get_if<Fenrir::StyleLinearGradient>(&style.backgroundImage);
    REQUIRE(gradient != nullptr);
    REQUIRE(gradient->stops.size() == 2);
    REQUIRE(Same(gradient->stops[0].colour, red));
    INFO("a text shadow's and a drop shadow's colour left out are red");
    REQUIRE(style.textShadow.size() == 1);
    REQUIRE(Same(style.textShadow[0].colour, red));
    REQUIRE(style.filter.size() == 1);
    REQUIRE(Same(style.filter[0].colour, red));
    INFO("scrollbar-color's currentcolor is the scroll view's colour");
    REQUIRE(Same(style.scrollbarColor.thumb, red));
    REQUIRE(Same(style.scrollbarColor.track, blue));
}

TEST_CASE("A pseudo-element's currentcolor is its own colour", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; overflow: scroll; }"
                         " .box::scrollbar-thumb { color: blue; background-color: currentcolor; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    const Fenrir::ResolvedStyle* thumb = t.root.GetPseudoElementStyle(box, Fenrir::PseudoElement::ScrollbarThumb);
    INFO("the thumb's background is its own blue, not the scroll view's red");
    REQUIRE(thumb != nullptr);
    REQUIRE(Same(thumb->backgroundColor, blue));
}

TEST_CASE("An inherited text shadow's currentcolor is the inheriting element's own colour", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".parent { color: red; text-shadow: 1px 1px; } .child { color: blue; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child");
    t.test.Frame();
    INFO("the parent's shadow is red; its child inherits the shadow, in its own blue, as CSS keeps currentcolor");
    REQUIRE(Same(t.Style(parent).textShadow.at(0).colour, red));
    REQUIRE(Same(t.Style(child).textShadow.at(0).colour, blue));
}

TEST_CASE("While color transitions, a currentcolor border is the text's colour each frame", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; border: 2px solid; transition: color 1s linear; }"
                         " .box.cool { color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    t.root.AddClass(box, "cool");
    for (int frame = 0; frame < 9; ++frame)
    {
        t.test.Frames(1, eighth);
        const Fenrir::ResolvedStyle& style = t.Style(box);
        INFO("frame " << frame << ": the border is the colour the text shows");
        REQUIRE(Same(style.borderTopColor, style.color));
    }
    INFO("past its second the text, and so the border, is blue");
    REQUIRE(Same(t.Style(box).color, blue));
}

TEST_CASE("A transition from currentcolor to a colour mixes the resolved colours", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; border-top-width: 1px; transition: border-top-color 1s linear; }"
                         " .box.blue { border-top-color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    t.root.AddClass(box, "blue");
    t.test.Frames(5, eighth);
    // Half way between sRGB (1, 0, 0) and (0, 0, 1) is sRGB (0.5, 0, 0.5), linear 0.21404.
    INFO("half way from currentcolor, red, to blue");
    REQUIRE(Same(t.Style(box).borderTopColor, Colour{0.21404f, 0.0f, 0.21404f, 1.0f}));
}

TEST_CASE("A keyframe animation from currentcolor mixes the colour it resolves to", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet("@keyframes cool { from { border-top-color: currentcolor; } to { border-top-color: blue; } }"
                         " .box { color: red; border-top-width: 1px; animation: cool 1s linear infinite; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(5, eighth);
    INFO("half way from currentcolor, red, to blue: sRGB (0.5, 0, 0.5), linear 0.21404");
    REQUIRE(Same(t.Style(box).borderTopColor, Colour{0.21404f, 0.0f, 0.21404f, 1.0f}));
}

TEST_CASE("inset sets top, right, bottom and left, and five values set none", "[ui][currentcolor]")
{
    ColourTest t;
    const ElementId box = t.Make("box");
    REQUIRE(t.root.SetStyleProperty(box, "inset", "1px 2px"));
    INFO("five values are refused");
    REQUIRE_FALSE(t.root.SetStyleProperty(box, "inset", "1px 2px 3px 4px 5px"));
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = t.Style(box);
    INFO("top and bottom are 1, right and left 2, as the two values gave them");
    REQUIRE(style.top == Fenrir::Length::Px(1.0f));
    REQUIRE(style.bottom == Fenrir::Length::Px(1.0f));
    REQUIRE(style.right == Fenrir::Length::Px(2.0f));
    REQUIRE(style.left == Fenrir::Length::Px(2.0f));
}

TEST_CASE("border-image resolves as its two longhands do, and refuses a width", "[ui][currentcolor]")
{
    ColourTest t;
    const Fenrir::AssetHandle<Fenrir::Image> frame = UITest::AddWhiteImage(t.test.app, "builtin:frame", 48, 48);
    t.root.AddStylesheet(".sheet { border-image: url(builtin:frame) 12; }", "sheet");
    const ElementId shorthand = t.Make("a");
    const ElementId longhands = t.Make("b");
    const ElementId sheet = t.Make("sheet");
    REQUIRE(t.root.SetStyleProperty(shorthand, "border-image", "url(builtin:frame) 12"));
    INFO("a slash inside the url() is part of its path");
    (void)UITest::AddWhiteImage(t.test.app, "builtin:ui/frame", 48, 48);
    REQUIRE(t.root.SetStyleProperty(t.Make("c"), "border-image", "url(builtin:ui/frame) 12"));
    REQUIRE(t.root.SetStyleProperty(longhands, "border-image-source", "url(builtin:frame)"));
    REQUIRE(t.root.SetStyleProperty(longhands, "border-image-slice", "12"));
    INFO("a width after a slash is refused");
    REQUIRE_FALSE(t.root.SetStyleProperty(shorthand, "border-image", "url(builtin:frame) 12 / 4px"));
    t.test.Frame();
    INFO("the shorthand's source and slices, inline or from a sheet, are the longhands'");
    for (const ElementId element : {shorthand, longhands, sheet})
    {
        REQUIRE(t.Style(element).borderImageSource == frame);
        const Fenrir::Sides& slice = t.Style(element).borderImageSlice;
        REQUIRE(slice.top == 12.0f);
        REQUIRE(slice.right == 12.0f);
        REQUIRE(slice.bottom == 12.0f);
        REQUIRE(slice.left == 12.0f);
    }
}
