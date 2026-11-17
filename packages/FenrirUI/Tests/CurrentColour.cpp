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
    // Half way from red to blue: sRGB (0.5, 0, 0.5), linear 0.21404, as CSS mixes in sRGB.
    constexpr Colour halfway{0.21404f, 0.0f, 0.21404f, 1.0f};

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

        // Sets `name: value` inline on `element`, which must read.
        void Set(ElementId element, std::string_view name, std::string_view value)
        {
            INFO("the inline declaration " << name << ": " << value << " reads");
            REQUIRE(root.SetStyleProperty(element, name, value));
        }
    };
} // namespace

TEST_CASE("currentcolor is a colour in any case", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; background-color: CurrentColor; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    INFO("background-color: CurrentColor reads, as the element's red");
    REQUIRE(Same(t.Style(box).backgroundColor, red));
}

TEST_CASE("color: currentcolor takes the parent's colour, as inherit does", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".parent { color: blue; } .child { color: red; } .child.own { color: CURRENTCOLOR; }",
                         "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.Make(parent, "child own");
    t.test.Frame();
    INFO("the later color: currentcolor is the parent's blue, not the earlier red");
    REQUIRE(Same(t.Style(child).color, blue));
}

TEST_CASE("A border shorthand with no colour gives every side the text colour", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; border: 2px solid; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = t.Style(box);
    INFO("every side is the text colour, red");
    REQUIRE(Same(style.borderTopColor, red));
    REQUIRE(Same(style.borderRightColor, red));
    REQUIRE(Same(style.borderBottomColor, red));
    REQUIRE(Same(style.borderLeftColor, red));
}

TEST_CASE("A border with no colour draws in the text colour", "[ui][currentcolor]")
{
    ColourTest t;
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
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; box-shadow: 0 2px 4px; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frame();
    INFO("one shadow, red");
    REQUIRE(t.Style(box).boxShadow.size() == 1);
    REQUIRE(Same(t.Style(box).boxShadow[0].colour, red));
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
    t.Set(child, "border-color", "currentcolor");
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
    const auto* gradient = std::get_if<Fenrir::StyleLinearGradient>(&style.backgroundImage);
    INFO("a gradient stop of currentcolor is red");
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

TEST_CASE("A pseudo-element's currentcolor follows its element's color transition", "[ui][currentcolor]")
{
    ColourTest t;
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
    REQUIRE(Same(thumb->backgroundColor, halfway));
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
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "cool");
    for (int frame = 1; frame <= 8; ++frame)
    {
        t.test.Frames(1, eighth);
        const Fenrir::ResolvedStyle& style = t.Style(box);
        INFO("frame " << frame << ": the border is the colour the text shows");
        REQUIRE(Same(style.borderTopColor, style.color));
        if (frame == 5)
        {
            INFO("half way, both are half way from red to blue");
            REQUIRE(Same(style.borderTopColor, halfway));
        }
    }
}

TEST_CASE("A transition from currentcolor to a colour mixes the resolved colours", "[ui][currentcolor]")
{
    ColourTest t;
    t.root.AddStylesheet(".box { color: red; border-top-width: 1px; transition: border-top-color 1s linear; }"
                         " .box.blue { border-top-color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "blue");
    t.test.Frames(5, eighth);
    INFO("half way from currentcolor, red, to blue");
    REQUIRE(Same(t.Style(box).borderTopColor, halfway));
}

TEST_CASE("A transition from currentcolor while color moves starts from the colour shown", "[ui][currentcolor]")
{
    ColourTest t;
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
    ColourTest t;
    t.root.AddStylesheet("@keyframes cool { from { border-top-color: currentcolor; } to { border-top-color: blue; } }"
                         " .box { color: red; border-top-width: 1px; animation: cool 1s linear infinite; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(5, eighth);
    INFO("half way from currentcolor, red, to blue");
    REQUIRE(Same(t.Style(box).borderTopColor, halfway));
}

TEST_CASE("inset: 1px 2px sets top and bottom to 1, right and left to 2", "[ui][shorthand]")
{
    ColourTest t;
    const ElementId box = t.Make("box");
    t.Set(box, "inset", "1px 2px");
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = t.Style(box);
    INFO("the first value is top and bottom, the second right and left");
    REQUIRE(style.top == Fenrir::Length::Px(1.0f));
    REQUIRE(style.bottom == Fenrir::Length::Px(1.0f));
    REQUIRE(style.right == Fenrir::Length::Px(2.0f));
    REQUIRE(style.left == Fenrir::Length::Px(2.0f));
}

TEST_CASE("inset with five values is refused and sets no side", "[ui][shorthand]")
{
    ColourTest t;
    const ElementId box = t.Make("box");
    INFO("five values are refused");
    REQUIRE_FALSE(t.root.SetStyleProperty(box, "inset", "1px 2px 3px 4px 5px"));
    t.test.Frame();
    INFO("every side keeps its initial auto");
    REQUIRE(t.Style(box).top == Fenrir::Length::Auto());
    REQUIRE(t.Style(box).left == Fenrir::Length::Auto());
}

TEST_CASE("border-image resolves as its two longhands do, inline or from a sheet", "[ui][shorthand]")
{
    ColourTest t;
    const Fenrir::AssetHandle<Fenrir::Image> frame = UITest::AddWhiteImage(t.test.app, "builtin:frame", 48, 48);
    t.root.AddStylesheet(".sheet { border-image: url(builtin:frame) 12; }", "sheet");
    const ElementId shorthand = t.Make("a");
    const ElementId longhands = t.Make("b");
    const ElementId sheet = t.Make("sheet");
    t.Set(shorthand, "border-image", "url(builtin:frame) 12");
    t.Set(longhands, "border-image-source", "url(builtin:frame)");
    t.Set(longhands, "border-image-slice", "12");
    t.test.Frame();
    for (const ElementId element : {shorthand, longhands, sheet})
    {
        const Fenrir::ResolvedStyle& style = t.Style(element);
        INFO("the source is the frame and every slice 12");
        REQUIRE(style.borderImageSource == frame);
        REQUIRE(style.borderImageSlice.top == 12.0f);
        REQUIRE(style.borderImageSlice.right == 12.0f);
        REQUIRE(style.borderImageSlice.bottom == 12.0f);
        REQUIRE(style.borderImageSlice.left == 12.0f);
    }
}

TEST_CASE("border-image refuses a width after a slash", "[ui][shorthand]")
{
    ColourTest t;
    (void)UITest::AddWhiteImage(t.test.app, "builtin:frame", 48, 48);
    const ElementId box = t.Make("box");
    INFO("url(builtin:frame) 12 / 4px is refused");
    REQUIRE_FALSE(t.root.SetStyleProperty(box, "border-image", "url(builtin:frame) 12 / 4px"));
}

TEST_CASE("border-image takes a slash inside its url() as part of the path", "[ui][shorthand]")
{
    ColourTest t;
    const Fenrir::AssetHandle<Fenrir::Image> frame = UITest::AddWhiteImage(t.test.app, "builtin:ui/frame", 48, 48);
    const ElementId box = t.Make("box");
    t.Set(box, "border-image", "url(builtin:ui/frame) 12");
    t.test.Frame();
    INFO("the path's slash is not a width's");
    REQUIRE(t.Style(box).borderImageSource == frame);
}
