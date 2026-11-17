// Element filters and blurred text shadows (the follow-ups spec's "Filters"): `filter` read from inline style and
// stylesheets, an element and its subtree drawn through one offscreen group per filter function, `filter`'s
// transitions, and a text shadow's blur, all read from the root's draw list through a capturing target (seam 2).
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirText/Paragraph.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "UITestApp.hpp"

namespace
{
    using Fenrir::ElementId;
    using Fenrir::FilterStep;

    constexpr double eighth = 0.125;

    // `name: value` pairs set on `element`'s inline style, each one taken.
    void Style(Fenrir::UIRoot& root, ElementId element,
               std::initializer_list<std::pair<const char*, const char*>> style)
    {
        for (const auto& [name, value] : style)
        {
            INFO("the inline declaration " << name << ": " << value << " is taken");
            REQUIRE(root.SetStyleProperty(element, name, value));
        }
    }

    // A red 100 by 50 panel at (10, 10) with a white 20 by 20 child at its top-left.
    ElementId PanelWithChild(Fenrir::UIRoot& root)
    {
        const ElementId panel = root.Create(root.GetRootElement(), "panel");
        Style(root, panel,
              {{"margin-left", "10px"},
               {"margin-top", "10px"},
               {"width", "100px"},
               {"height", "50px"},
               {"background-color", "#ff0000"}});
        const ElementId child = root.Create(panel, "panel");
        Style(root, child, {{"width", "20px"}, {"height", "20px"}, {"background-color", "#ffffff"}});
        return panel;
    }

    const std::vector<FilterStep>& FilterOf(const Fenrir::UIRoot& root, ElementId element)
    {
        const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
        INFO("the element is alive");
        REQUIRE(style != nullptr);
        return style->filter;
    }

    bool Same(const Fenrir::Colour& a, const Fenrir::Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    bool IsBegin(const Fenrir::Command2D& command)
    {
        return std::holds_alternative<Fenrir::BeginGroup2D>(command);
    }

    bool IsEnd(const Fenrir::Command2D& command)
    {
        return std::holds_alternative<Fenrir::EndGroup2D>(command);
    }

    bool IsDraw(const Fenrir::Command2D& command)
    {
        return std::holds_alternative<Fenrir::DrawCommand2D>(command);
    }

    bool HasGroup(const Fenrir::DrawList2D& list)
    {
        return std::any_of(list.commands.begin(), list.commands.end(), IsBegin);
    }

    std::size_t DrawnIndices(const Fenrir::DrawList2D& list, std::size_t first, std::size_t last)
    {
        std::size_t count = 0;
        for (std::size_t i = first; i < last; ++i)
        {
            if (const auto* draw = std::get_if<Fenrir::DrawCommand2D>(&list.commands[i]))
            {
                count += draw->indexCount;
            }
        }
        return count;
    }

    std::size_t AllIndices(const Fenrir::DrawList2D& list)
    {
        return DrawnIndices(list, 0, list.commands.size());
    }

    bool HasDiagnosticOnLine(const Fenrir::Stylesheet& sheet, std::uint32_t line, std::string_view mentions)
    {
        return std::any_of(sheet.diagnostics.begin(), sheet.diagnostics.end(), [&](const Fenrir::StyleDiagnostic& d) {
            return d.line == line && d.message.find(mentions) != std::string::npos;
        });
    }
} // namespace

TEST_CASE("filter reads its functions in the order written, or none, into the resolved list", "[ui][style][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId panel = root.Create(root.GetRootElement(), "panel");

    INFO("blur() of a length in px is taken");
    REQUIRE(root.SetStyleProperty(panel, "filter", "blur(4px)"));
    test.Frame();
    {
        const std::vector<FilterStep>& filter = FilterOf(root, panel);
        INFO("blur(4px) is one blur of radius 4");
        REQUIRE(filter.size() == 1);
        REQUIRE(filter[0].kind == FilterStep::Kind::Blur);
        REQUIRE(filter[0].radius == 4.0f);
    }

    INFO("drop-shadow() with its offset, blur and colour is taken");
    REQUIRE(root.SetStyleProperty(panel, "filter", "drop-shadow(2px 3px 4px red)"));
    test.Frame();
    {
        const std::vector<FilterStep>& filter = FilterOf(root, panel);
        INFO("drop-shadow(2px 3px 4px red) is one red shadow 2 across and 3 down, blurred by 4");
        REQUIRE(filter.size() == 1);
        REQUIRE(filter[0].kind == FilterStep::Kind::DropShadow);
        REQUIRE(filter[0].offset == Fenrir::Math::Vec2(2.0f, 3.0f));
        REQUIRE(filter[0].radius == 4.0f);
        REQUIRE(Same(filter[0].colour, Fenrir::Colour::Red));
    }

    INFO("drop-shadow() with a colour function, commas inside it, is taken");
    REQUIRE(root.SetStyleProperty(panel, "filter", "drop-shadow(0 4px 6px rgba(0, 0, 0, 0.5))"));
    test.Frame();
    {
        const std::vector<FilterStep>& filter = FilterOf(root, panel);
        INFO("drop-shadow(0 4px 6px rgba(0, 0, 0, 0.5)) is one half-transparent black shadow 4 down, blurred by 6");
        REQUIRE(filter.size() == 1);
        REQUIRE(filter[0].offset == Fenrir::Math::Vec2(0.0f, 4.0f));
        REQUIRE(filter[0].radius == 6.0f);
        REQUIRE(filter[0].colour.a == 0.5f);
    }

    INFO("drop-shadow() with only its offset is taken");
    REQUIRE(root.SetStyleProperty(panel, "filter", "drop-shadow(-1px 2px)"));
    REQUIRE(root.SetStyleProperty(panel, "color", "blue"));
    test.Frame();
    {
        const std::vector<FilterStep>& filter = FilterOf(root, panel);
        INFO("a drop shadow with no blur or colour is sharp and in the text colour, currentcolor");
        REQUIRE(filter.size() == 1);
        REQUIRE(filter[0].offset == Fenrir::Math::Vec2(-1.0f, 2.0f));
        REQUIRE(filter[0].radius == 0.0f);
        REQUIRE(Same(filter[0].colour, Fenrir::Colour::Blue));
    }

    INFO("a list of both is taken");
    REQUIRE(root.SetStyleProperty(panel, "filter", "blur(4px) drop-shadow(2px 3px 4px red)"));
    test.Frame();
    {
        const std::vector<FilterStep>& filter = FilterOf(root, panel);
        INFO("the list keeps its functions in the order written");
        REQUIRE(filter.size() == 2);
        REQUIRE(filter[0].kind == FilterStep::Kind::Blur);
        REQUIRE(filter[1].kind == FilterStep::Kind::DropShadow);
    }

    INFO("none is taken");
    REQUIRE(root.SetStyleProperty(panel, "filter", "none"));
    test.Frame();
    INFO("none is no filter");
    REQUIRE(FilterOf(root, panel).empty());
}

TEST_CASE("A filter a stylesheet cannot read is logged with its line and skipped", "[ui][stylesheet][filter]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("panel {\n"
                                                               "  filter: blur(-1px);\n"
                                                               "  filter: sepia(1);\n"
                                                               "  filter: drop-shadow(red);\n"
                                                               "  filter: drop-shadow(1px 1px, 2px 2px);\n"
                                                               "  width: 1px;\n"
                                                               "}\n",
                                                               "filters");
    INFO("a negative blur is refused, on line 2");
    REQUIRE(HasDiagnosticOnLine(sheet, 2, "filter"));
    INFO("a filter function FenrirUI does not draw is refused, on line 3");
    REQUIRE(HasDiagnosticOnLine(sheet, 3, "filter"));
    INFO("a drop shadow without its offset is refused, on line 4");
    REQUIRE(HasDiagnosticOnLine(sheet, 4, "filter"));
    INFO("two shadows in one drop shadow are refused, on line 5");
    REQUIRE(HasDiagnosticOnLine(sheet, 5, "filter"));
    INFO("those four are the sheet's only diagnostics");
    REQUIRE(sheet.diagnostics.size() == 4);
    INFO("the four declarations are skipped and the rest of the rule stands");
    REQUIRE(sheet.rules.at(0).declarations.size() == 1);
    REQUIRE(sheet.rules.at(0).declarations[0].name == "width");
}

TEST_CASE("filter: blur() draws the element and its subtree through one blur group over what they cover",
          "[ui][paint][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId panel = PanelWithChild(root);
    const ElementId wide = root.Create(panel, "panel");
    Style(root, wide, {{"width", "150px"}, {"height", "10px"}, {"flex-shrink", "0"}, {"background-color", "#00ff00"}});
    Style(root, panel, {{"filter", "blur(4px)"}});
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    INFO("a group opens first and closes last, with only draws between: " << list.commands.size() << " commands");
    REQUIRE(list.commands.size() >= 3);
    REQUIRE(IsBegin(list.commands.front()));
    REQUIRE(IsEnd(list.commands.back()));
    REQUIRE(std::all_of(list.commands.begin() + 1, list.commands.end() - 1, IsDraw));
    INFO("the panel's and both children's boxes, six triangles, are drawn inside it");
    REQUIRE(AllIndices(list) == 18);

    const auto& begin = std::get<Fenrir::BeginGroup2D>(list.commands.front());
    INFO("the group covers the panel (10, 10, 100 by 50) and the wide child running to x 160, grown by 4: "
         << begin.bounds.x << ", " << begin.bounds.y << ", " << begin.bounds.width << " by " << begin.bounds.height);
    REQUIRE(UITest::Near(begin.bounds, {6.0f, 6.0f, 158.0f, 58.0f}));
    const auto& end = std::get<Fenrir::EndGroup2D>(list.commands.back());
    INFO("the group ends as a blur of radius 4, at full opacity");
    REQUIRE(end.filter == Fenrir::GroupFilter2D::Blur);
    REQUIRE(UITest::Near(end.radius, 4.0f));
    REQUIRE(end.opacity == 1.0f);
}

TEST_CASE("A filter list draws its first function in the innermost group", "[ui][paint][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId panel = PanelWithChild(root);
    Style(root, panel, {{"filter", "drop-shadow(2px 3px 4px red) blur(5px)"}});
    test.Frame();

    {
        const Fenrir::DrawList2D& list = target->lists.back();
        const std::size_t n = list.commands.size();
        INFO("two groups open first and close last: " << n << " commands");
        REQUIRE(n >= 5);
        REQUIRE(IsBegin(list.commands[0]));
        REQUIRE(IsBegin(list.commands[1]));
        REQUIRE(IsEnd(list.commands[n - 2]));
        REQUIRE(IsEnd(list.commands[n - 1]));
        REQUIRE(std::all_of(list.commands.begin() + 2, list.commands.end() - 2, IsDraw));

        const auto& inner = std::get<Fenrir::EndGroup2D>(list.commands[n - 2]);
        INFO("the inner group is the drop shadow: red, 2 across and 3 down, blurred by 4");
        REQUIRE(inner.filter == Fenrir::GroupFilter2D::DropShadow);
        REQUIRE(UITest::Near(inner.radius, 4.0f));
        REQUIRE(inner.offset == Fenrir::Math::Vec2(2.0f, 3.0f));
        REQUIRE(inner.shadowColour == Fenrir::Math::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        const auto& outer = std::get<Fenrir::EndGroup2D>(list.commands[n - 1]);
        INFO("the outer group is the blur of 5");
        REQUIRE(outer.filter == Fenrir::GroupFilter2D::Blur);
        REQUIRE(UITest::Near(outer.radius, 5.0f));

        const auto& innerBegin = std::get<Fenrir::BeginGroup2D>(list.commands[1]);
        INFO("the drop shadow's group covers the panel and its shadow (moved by (2, 3), grown by 4): "
             << innerBegin.bounds.x << ", " << innerBegin.bounds.y << ", " << innerBegin.bounds.width << " by "
             << innerBegin.bounds.height);
        REQUIRE(UITest::Near(innerBegin.bounds, {8.0f, 9.0f, 108.0f, 58.0f}));
        const auto& outerBegin = std::get<Fenrir::BeginGroup2D>(list.commands[0]);
        INFO("the blur's group covers that grown by 5");
        REQUIRE(UITest::Near(outerBegin.bounds, {3.0f, 4.0f, 118.0f, 68.0f}));
    }
}

TEST_CASE("An element's opacity group is outside its filter groups", "[ui][paint][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId panel = PanelWithChild(root);
    Style(root, panel, {{"filter", "drop-shadow(2px 3px 4px red) blur(5px)"}, {"opacity", "0.5"}});
    test.Frame();
    {
        const Fenrir::DrawList2D& list = target->lists.back();
        const std::size_t n = list.commands.size();
        INFO("three groups open first and close last: " << n << " commands");
        REQUIRE(n >= 7);
        REQUIRE(std::all_of(list.commands.begin(), list.commands.begin() + 3, IsBegin));
        REQUIRE(std::all_of(list.commands.end() - 3, list.commands.end(), IsEnd));
        const auto& outermost = std::get<Fenrir::EndGroup2D>(list.commands[n - 1]);
        INFO("the outermost group is the opacity's, with no filter");
        REQUIRE(outermost.filter == Fenrir::GroupFilter2D::None);
        REQUIRE(UITest::Near(outermost.opacity, 0.5f));
        REQUIRE(std::get<Fenrir::EndGroup2D>(list.commands[n - 2]).filter == Fenrir::GroupFilter2D::Blur);
        REQUIRE(std::get<Fenrir::EndGroup2D>(list.commands[n - 3]).filter == Fenrir::GroupFilter2D::DropShadow);
        INFO("the opacity's group covers what the blur's does");
        REQUIRE(UITest::Near(std::get<Fenrir::BeginGroup2D>(list.commands[0]).bounds, {3.0f, 4.0f, 118.0f, 68.0f}));
    }
}

TEST_CASE("A filter transition mixes the radius of two blurs", "[ui][motion][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    root.AddStylesheet(".box { filter: blur(0px); transition: filter 1s linear; } .box.blurred { filter: blur(10px); }",
                       "sheet");
    const ElementId box = root.Create(root.GetRootElement(), "panel");
    root.AddClass(box, "box");
    test.Frames(1, eighth);
    root.AddClass(box, "blurred");
    test.Frames(5, eighth);

    const std::vector<FilterStep>& filter = FilterOf(root, box);
    INFO("half a second into a one-second linear transition from blur(0px) to blur(10px), it shows blur(5px)");
    REQUIRE(filter.size() == 1);
    REQUIRE(filter[0].kind == FilterStep::Kind::Blur);
    REQUIRE(UITest::Near(filter[0].radius, 5.0f));
}

TEST_CASE("A filter transition between lists of different functions switches at the midpoint", "[ui][motion][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    root.AddStylesheet(".box { transition: filter 1s linear; } .box.blurred { filter: blur(10px); }", "sheet");
    const ElementId box = root.Create(root.GetRootElement(), "panel");
    root.AddClass(box, "box");
    test.Frames(1, eighth);
    root.AddClass(box, "blurred");
    test.Frames(4, eighth);
    INFO("three eighths of a second into none to blur(10px), it still shows none");
    REQUIRE(FilterOf(root, box).empty());
    test.Frames(1, eighth);
    INFO("half way, it shows blur(10px)");
    REQUIRE(FilterOf(root, box).size() == 1);
    REQUIRE(FilterOf(root, box)[0].radius == 10.0f);
}

TEST_CASE("A text shadow without a blur draws its runs directly, in no group", "[ui][paint][text][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId label = root.Create(root.GetRootElement(), "label");
    root.SetText(label, "Glow");
    Style(root, label, {{"color", "#0000ff"}, {"text-shadow", "2px 3px #ff0000"}});
    test.Frame();
    INFO("a sharp text shadow draws in no group");
    REQUIRE_FALSE(HasGroup(target->lists.back()));
}

TEST_CASE("A text shadow with a blur draws its runs inside a blur group over the paragraph's box",
          "[ui][paint][text][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId label = root.Create(root.GetRootElement(), "label");
    root.SetText(label, "Glow");
    Style(root, label, {{"color", "#0000ff"}, {"text-shadow", "2px 3px 4px #ff0000"}});
    test.Frame();
    const Fenrir::DrawList2D& list = target->lists.back();
    INFO("a group round the shadow's runs, then the text's own: " << list.commands.size() << " commands");
    REQUIRE(list.commands.size() == 4);
    REQUIRE(IsBegin(list.commands[0]));
    REQUIRE(IsDraw(list.commands[1]));
    REQUIRE(IsEnd(list.commands[2]));
    REQUIRE(IsDraw(list.commands[3]));
    INFO("the shadow draws as many glyphs as the text");
    REQUIRE(DrawnIndices(list, 1, 2) == DrawnIndices(list, 3, 4));
    const auto& end = std::get<Fenrir::EndGroup2D>(list.commands[2]);
    INFO("the group ends as a blur of the shadow's 4");
    REQUIRE(end.filter == Fenrir::GroupFilter2D::Blur);
    REQUIRE(UITest::Near(end.radius, 4.0f));

    const Fenrir::Rect2D content = root.GetBoxes(label).content;
    const Fenrir::Paragraph* paragraph = root.GetParagraph(label);
    INFO("the label has a paragraph");
    REQUIRE(paragraph != nullptr);
    const Fenrir::Math::Vec2 size = paragraph->Size();
    const auto& begin = std::get<Fenrir::BeginGroup2D>(list.commands[0]);
    INFO("the group is the paragraph's box moved by (2, 3) and grown by 4 (one target pixel per UI unit)");
    REQUIRE(UITest::Near(begin.bounds, {content.x - 2.0f, content.y - 1.0f, size.x + 8.0f, size.y + 8.0f}));
}

TEST_CASE("Each blurred text shadow draws in a blur group of its own, the last drawn first",
          "[ui][paint][text][filter]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const ElementId label = root.Create(root.GetRootElement(), "label");
    root.SetText(label, "Glow");
    Style(root, label, {{"text-shadow", "1px 1px 2px #ff0000, 0 0 6px #00ff00"}});
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    INFO("two groups, each round one shadow's runs, then the text's own: " << list.commands.size() << " commands");
    REQUIRE(list.commands.size() == 7);
    REQUIRE(IsBegin(list.commands[0]));
    REQUIRE(IsEnd(list.commands[2]));
    REQUIRE(IsBegin(list.commands[3]));
    REQUIRE(IsEnd(list.commands[5]));
    REQUIRE(IsDraw(list.commands[6]));
    INFO("the second shadow, under the first, draws first: blurred by 6, then the first's 2");
    REQUIRE(UITest::Near(std::get<Fenrir::EndGroup2D>(list.commands[2]).radius, 6.0f));
    REQUIRE(UITest::Near(std::get<Fenrir::EndGroup2D>(list.commands[5]).radius, 2.0f));
}

namespace
{
    // The list drawn with groups on, then the one drawn with them off, for a root `setUp` made.
    template <typename SetUp>
    std::pair<Fenrir::DrawList2D, Fenrir::DrawList2D> OnAndOff(SetUp setUp)
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
        setUp(root);
        test.Frame();
        Fenrir::DrawList2D on = target->lists.back();
        test.Roots().settings.offscreenGroups = false;
        test.Frame();
        return {std::move(on), target->lists.back()};
    }
} // namespace

TEST_CASE("With offscreen groups off, an element's filter is ignored", "[ui][paint][filter]")
{
    const auto [on, off] = OnAndOff([](Fenrir::UIRoot& root) {
        Style(root, PanelWithChild(root), {{"filter", "drop-shadow(2px 3px 4px red) blur(5px)"}});
    });
    INFO("with groups on, the filter draws groups");
    REQUIRE(HasGroup(on));
    INFO("with groups off, it draws none, and the same boxes are drawn");
    REQUIRE_FALSE(HasGroup(off));
    REQUIRE(AllIndices(off) == AllIndices(on));
}

TEST_CASE("With offscreen groups off, a blurred text shadow draws sharp", "[ui][paint][text][filter]")
{
    const auto [on, off] = OnAndOff([](Fenrir::UIRoot& root) {
        const ElementId label = root.Create(root.GetRootElement(), "label");
        root.SetText(label, "Glow");
        Style(root, label, {{"text-shadow", "2px 3px 4px #ff0000"}});
    });
    INFO("with groups on, the shadow draws in a group");
    REQUIRE(HasGroup(on));
    INFO("with groups off, it draws in none, and the shadow's glyphs are still drawn");
    REQUIRE_FALSE(HasGroup(off));
    REQUIRE(AllIndices(off) == AllIndices(on));
}
