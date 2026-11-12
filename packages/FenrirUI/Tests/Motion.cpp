// Transitions, keyframe animations, :intro, :outro and deferred removal through a headless App (the spec's seam 1),
// ticking fixed frame deltas of an eighth of a second, which add up exactly in binary floating point.
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "UITestApp.hpp"

namespace
{
    using Fenrir::ElementId;
    using Fenrir::PseudoClass;
    using Fenrir::RemoveMode;

    constexpr double eighth = 0.125;

    struct MotionTest
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

        ElementId Make(std::string_view classes)
        {
            const ElementId element = root.Create(root.GetRootElement(), "panel");
            for (std::size_t start = 0; start < classes.size();)
            {
                const std::size_t end = std::min(classes.find(' ', start), classes.size());
                root.AddClass(element, classes.substr(start, end - start));
                start = end + 1;
            }
            return element;
        }

        float Width(ElementId element) const
        {
            const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
            INFO("the element is alive and its width is in px");
            REQUIRE(style != nullptr);
            REQUIRE(style->width.unit == Fenrir::LengthUnit::Px);
            return style->width.value;
        }

        float Opacity(ElementId element) const
        {
            const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
            INFO("the element is alive");
            REQUIRE(style != nullptr);
            return style->opacity;
        }
    };
} // namespace

TEST_CASE("A width transition is half way at half its duration under linear", "[ui][motion]")
{
    MotionTest t;
    t.root.AddStylesheet(".box { width: 0px; transition: width 1s linear; } .box.wide { width: 100px; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    INFO("an element's first style transitions from nothing");
    REQUIRE(t.Width(box) == 0.0f);

    t.root.AddClass(box, "wide");
    t.test.Frames(1, eighth);
    INFO("the frame the change is seen in starts the transition at its old value");
    REQUIRE(UITest::Near(t.Width(box), 0.0f));
    t.test.Frames(4, eighth);
    INFO("half a second into a one-second linear transition from 0 to 100, it shows 50");
    REQUIRE(UITest::Near(t.Width(box), 50.0f));
    t.test.Frames(4, eighth);
    INFO("at its end it shows the new value");
    REQUIRE(t.Width(box) == 100.0f);
}

TEST_CASE("Ease-in-out and steps(4) transitions show the fractions their curves give", "[ui][motion]")
{
    MotionTest t;
    t.root.AddStylesheet(".box { width: 0px; } .box.wide { width: 100px; }\n"
                         ".smooth { transition: width 1s ease-in-out; }\n"
                         ".stepped { transition: width 1s steps(4); }",
                         "sheet");
    const ElementId smooth = t.Make("box smooth");
    const ElementId stepped = t.Make("box stepped");
    t.test.Frames(1, eighth);
    t.root.AddClass(smooth, "wide");
    t.root.AddClass(stepped, "wide");
    t.test.Frames(1, eighth);

    t.test.Frames(2, eighth);
    // cubic-bezier(0.42, 0, 0.58, 1) at x = 0.25 has y = 0.12916, solved by bisection outside the engine.
    INFO("a quarter through, ease-in-out has gone 12.9% of the way");
    REQUIRE(UITest::Near(t.Width(smooth), 12.916f, 0.01f));
    t.test.Frames(1, eighth);
    INFO("three eighths through, steps(4) holds its first step, a quarter of the way");
    REQUIRE(t.Width(stepped) == 25.0f);
    t.test.Frames(2, eighth);
    INFO("five eighths through, steps(4) holds its second step, half way");
    REQUIRE(t.Width(stepped) == 50.0f);
}

TEST_CASE("A colour transition interpolates in sRGB", "[ui][motion]")
{
    MotionTest t;
    t.root.AddStylesheet(".box { background-color: red; transition: background-color 1s linear; }\n"
                         ".box.blue { background-color: blue; }",
                         "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "blue");
    t.test.Frames(5, eighth);
    const Fenrir::Colour colour = t.root.GetResolvedStyle(box)->backgroundColor;
    // Half way between sRGB (1, 0, 0) and (0, 0, 1) is sRGB (0.5, 0, 0.5), which is 0.21404 in linear; a linear mix
    // would give 0.5.
    INFO("half way from red to blue, red and blue are each sRGB 0.5, linear 0.214");
    REQUIRE(UITest::Near(colour.r, 0.21404f));
    REQUIRE(UITest::Near(colour.g, 0.0f));
    REQUIRE(UITest::Near(colour.b, 0.21404f));
    REQUIRE(UITest::Near(colour.a, 1.0f));
}

TEST_CASE("A change mid-transition continues from the value shown", "[ui][motion]")
{
    MotionTest t;
    t.root.AddStylesheet(".box { width: 0px; transition: width 1s linear; } .box.wide { width: 100px; }", "sheet");
    const ElementId box = t.Make("box");
    t.test.Frames(1, eighth);
    t.root.AddClass(box, "wide");
    t.test.Frames(5, eighth);
    REQUIRE(UITest::Near(t.Width(box), 50.0f));

    t.root.RemoveClass(box, "wide");
    t.test.Frames(1, eighth);
    // The frame that sees the change is an eighth of a second on: the old transition stood at 62.5 then.
    INFO("the new transition starts where the old one had got to, not at either end");
    REQUIRE(UITest::Near(t.Width(box), 62.5f));
    t.test.Frames(4, eighth);
    INFO("half way back from 62.5 to 0 over a second");
    REQUIRE(UITest::Near(t.Width(box), 31.25f));
}

TEST_CASE("A keyframe animation loops, reverses as its direction says and holds with fill-mode forwards",
          "[ui][motion]")
{
    MotionTest t;
    t.root.AddStylesheet("@keyframes grow { from { width: 0px; } to { width: 100px; } }\n"
                         ".loop { animation: grow 1s linear infinite; }\n"
                         ".alternate { animation: grow 1s linear infinite alternate; }\n"
                         ".hold { animation: grow 1s linear 1 forwards; }\n"
                         ".once { width: 7px; animation: grow 1s linear 1; }",
                         "sheet");
    const ElementId loop = t.Make("loop");
    const ElementId alternate = t.Make("alternate");
    const ElementId hold = t.Make("hold");
    const ElementId once = t.Make("once");
    t.test.Frames(5, eighth);
    INFO("half way through the first iteration every one shows 50");
    REQUIRE(UITest::Near(t.Width(loop), 50.0f));
    REQUIRE(UITest::Near(t.Width(alternate), 50.0f));
    REQUIRE(UITest::Near(t.Width(hold), 50.0f));
    REQUIRE(UITest::Near(t.Width(once), 50.0f));

    t.test.Frames(6, eighth);
    INFO("a quarter into the second iteration: infinite starts again from 0");
    REQUIRE(UITest::Near(t.Width(loop), 25.0f));
    INFO("alternate plays its second iteration backwards, from 100");
    REQUIRE(UITest::Near(t.Width(alternate), 75.0f));
    INFO("fill-mode forwards holds the end once its one iteration is over");
    REQUIRE(t.Width(hold) == 100.0f);
    INFO("with no fill, its cascaded width is back once it ends");
    REQUIRE(t.Width(once) == 7.0f);
}

TEST_CASE("An element has :intro on its first frame and not on its second", "[ui][motion][lifecycle]")
{
    MotionTest t;
    t.root.AddStylesheet(".a { width: 20px; } .a:intro { width: 10px; }", "sheet");
    const ElementId element = t.Make("a");
    INFO("it is made with :intro");
    REQUIRE(t.root.HasPseudoClass(element, PseudoClass::Intro));

    t.test.Frames(1, eighth);
    INFO("its first frame is styled with its :intro rules, and the layout that completes clears it");
    REQUIRE(t.Width(element) == 10.0f);
    REQUIRE_FALSE(t.root.HasPseudoClass(element, PseudoClass::Intro));
    t.test.Frames(1, eighth);
    INFO("its second frame is styled without them");
    REQUIRE(t.Width(element) == 20.0f);
}

TEST_CASE("Remove() keeps an element with an outro transition alive and animating for its duration, then removes it",
          "[ui][motion][lifecycle]")
{
    MotionTest t;
    t.root.AddStylesheet(".toast { transition: opacity 1s linear; } .toast:outro { opacity: 0; }", "sheet");
    const ElementId toast = t.Make("toast");
    t.test.Frames(2, eighth);

    REQUIRE(t.root.Remove(toast));
    INFO("it carries :outro and is alive until its outro ends");
    REQUIRE(t.root.HasPseudoClass(toast, PseudoClass::Outro));
    REQUIRE(t.root.IsAlive(toast));
    t.test.Frames(1, eighth);
    REQUIRE(t.root.IsAlive(toast));
    t.test.Frames(4, eighth);
    INFO("half way through its outro it is alive at half opacity");
    REQUIRE(t.root.IsAlive(toast));
    REQUIRE(UITest::Near(t.Opacity(toast), 0.5f));
    t.test.Frames(3, eighth);
    INFO("an eighth of a second before the end it is still alive");
    REQUIRE(t.root.IsAlive(toast));
    REQUIRE(UITest::Near(t.Opacity(toast), 0.125f));
    t.test.Frames(1, eighth);
    INFO("once the transition ends it is removed and its id is stale");
    REQUIRE_FALSE(t.root.IsAlive(toast));
    REQUIRE(t.root.GetChildren(t.root.GetRootElement()).empty());
}

TEST_CASE("Remove() with no outro transition removes the element after the next frame", "[ui][motion][lifecycle]")
{
    MotionTest t;
    const ElementId plain = t.Make("plain");
    t.test.Frames(1, eighth);
    REQUIRE(t.root.Remove(plain));
    INFO("it is alive until the root's next frame");
    REQUIRE(t.root.IsAlive(plain));
    t.test.Frames(1, eighth);
    INFO("with nothing to transition, that frame removes it");
    REQUIRE_FALSE(t.root.IsAlive(plain));
}

TEST_CASE("Remove(immediate) removes an element at once, outro transition or not", "[ui][motion][lifecycle]")
{
    MotionTest t;
    t.root.AddStylesheet(".toast { transition: opacity 1s linear; } .toast:outro { opacity: 0; }", "sheet");
    const ElementId toast = t.Make("toast");
    const ElementId leaving = t.Make("toast");
    t.test.Frames(1, eighth);

    REQUIRE(t.root.Remove(toast, RemoveMode::Immediate));
    INFO("its id is stale with no frame run");
    REQUIRE_FALSE(t.root.IsAlive(toast));

    REQUIRE(t.root.Remove(leaving));
    t.test.Frames(2, eighth);
    REQUIRE(t.root.IsAlive(leaving));
    REQUIRE(t.root.Remove(leaving, RemoveMode::Immediate));
    INFO("an outro under way is cut short");
    REQUIRE_FALSE(t.root.IsAlive(leaving));
    t.test.Frames(1, eighth);
}

TEST_CASE("A parent's outro keeps its children alive while their transitions run", "[ui][motion][lifecycle]")
{
    MotionTest t;
    t.root.AddStylesheet(".child { transition: opacity 0.5s linear; } .parent:outro .child { opacity: 0; }", "sheet");
    const ElementId parent = t.Make("parent");
    const ElementId child = t.root.Create(parent, "panel");
    t.root.AddClass(child, "child");
    t.test.Frames(2, eighth);

    REQUIRE(t.root.Remove(parent));
    t.test.Frames(4, eighth);
    INFO("the parent has no transition of its own, but its child's keeps both alive");
    REQUIRE(t.root.IsAlive(parent));
    REQUIRE(t.root.IsAlive(child));
    REQUIRE(UITest::Near(t.Opacity(child), 0.25f));
    t.test.Frames(1, eighth);
    INFO("when the child's transition ends both go");
    REQUIRE_FALSE(t.root.IsAlive(parent));
    REQUIRE_FALSE(t.root.IsAlive(child));
}
