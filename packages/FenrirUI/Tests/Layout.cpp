// The layout bridge through a headless App (the spec's seam 1): element trees with inline styles in, rectangles out.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "UITestApp.hpp"

namespace
{
    struct FixtureNode
    {
        int parent;
        const char* style;
        Fenrir::Rect2D rect;
    };

    struct Fixture
    {
        const char* name;
        std::vector<FixtureNode> nodes;
    };

    // Yoga's generated conformance fixtures, converted by fixtures/convert-yoga-fixtures.py.
    const std::vector<Fixture>& Fixtures()
    {
        static const std::vector<Fixture> fixtures = {
#include "fixtures/YogaFixtures.inc"
        };
        return fixtures;
    }

    std::string_view Trim(std::string_view text)
    {
        while (!text.empty() && text.front() == ' ')
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && text.back() == ' ')
        {
            text.remove_suffix(1);
        }
        return text;
    }

    // Gives `element` each `name: value` of `style`, through the root's property-by-name setter.
    void SetInlineStyle(Fenrir::UIRoot& root, Fenrir::ElementId element, std::string_view style)
    {
        while (!style.empty())
        {
            const std::size_t end = style.find(';');
            const std::string_view declaration = style.substr(0, end);
            style = end == std::string_view::npos ? std::string_view{} : style.substr(end + 1);
            const std::size_t colon = declaration.find(':');
            if (colon == std::string_view::npos)
            {
                continue;
            }
            const std::string_view name = Trim(declaration.substr(0, colon));
            const std::string_view value = Trim(declaration.substr(colon + 1));
            INFO("the inline style property " << name << " takes the value " << value);
            REQUIRE(root.SetStyleProperty(element, name, value));
        }
    }

    // Fixtures that test Yoga's own layout root, which a FenrirUI root element never is: each fixture's root is laid
    // out here as an absolute child of the root element, which fills its target, so it has a containing block where
    // Yoga's root has none (percentages of an undefined size, and flex-grow and max sizes on the root that Yoga
    // ignores), and Yoga rounds positions relative to the parent where the root snaps each absolute edge.
    bool IsYogaRootOnly(std::string_view name)
    {
        constexpr std::array<std::string_view, 5> rootOnly{
            "YGGapTest/row_gap_percent_determines_parent_height",
            "YGMinMaxDimensionTest/flex_grow_within_constrained_max_column",
            "YGMinMaxDimensionTest/flex_root_ignored",
            "YGPercentageTest/percentage_flex_basis_main_min_width",
            "YGRoundingTest/rounding_fractial_input_3",
        };
        return std::find(rootOnly.begin(), rootOnly.end(), name) != rootOnly.end();
    }

    // `rect` moved to be relative to `origin`'s top-left.
    Fenrir::Rect2D RelativeTo(const Fenrir::Rect2D& rect, const Fenrir::Rect2D& origin)
    {
        return {rect.x - origin.x, rect.y - origin.y, rect.width, rect.height};
    }
} // namespace

TEST_CASE("Yoga's conformance fixtures lay out to their rectangles through the bridge", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    // Wide enough that no fixture's absolute root is narrowed by its containing block, at one pixel per UI unit, so
    // snapping rounds to whole UI units as Yoga's own rounding does.
    Fenrir::UIRoot& root = test.MakeRoot(target, {4000.0f, 4000.0f});

    std::size_t checked = 0;
    for (const Fixture& fixture : Fixtures())
    {
        if (IsYogaRootOnly(fixture.name))
        {
            continue;
        }
        std::vector<Fenrir::ElementId> elements;
        for (const FixtureNode& node : fixture.nodes)
        {
            const Fenrir::ElementId parent =
                node.parent < 0 ? root.GetRootElement() : elements[static_cast<std::size_t>(node.parent)];
            elements.push_back(root.Create(parent, "panel"));
            SetInlineStyle(root, elements.back(), node.style);
        }
        test.Frame();

        for (std::size_t i = 0; i < fixture.nodes.size(); ++i)
        {
            const FixtureNode& node = fixture.nodes[i];
            const Fenrir::Rect2D parentBox =
                node.parent < 0 ? root.GetBoxes(root.GetRootElement()).border
                                : root.GetBoxes(elements[static_cast<std::size_t>(node.parent)]).border;
            const Fenrir::Rect2D laidOut = RelativeTo(root.GetBoxes(elements[i]).border, parentBox);
            INFO(fixture.name << ": node " << i << " is laid out at "
                              << Catch::StringMaker<Fenrir::Rect2D>::convert(laidOut) << ", where Yoga puts it at "
                              << Catch::StringMaker<Fenrir::Rect2D>::convert(node.rect));
            REQUIRE(UITest::Near(laidOut, node.rect));
            ++checked;
        }
        root.Remove(elements.front(), Fenrir::RemoveMode::Immediate);
    }
    INFO("every converted fixture was checked");
    REQUIRE(checked > 1000);
}

TEST_CASE("Absolute children are placed by their insets in the parent's padding box", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    root.EditInlineStyle(panel)
        ->SetWidth(Fenrir::Length::Px(200.0f))
        .SetHeight(Fenrir::Length::Px(100.0f))
        .SetPadding(Fenrir::Length::Px(10.0f), Fenrir::Length::Px(10.0f), Fenrir::Length::Px(10.0f),
                    Fenrir::Length::Px(10.0f));
    const Fenrir::ElementId badge = root.Create(panel, "panel");
    root.EditInlineStyle(badge)
        ->SetPosition(Fenrir::Position::Absolute)
        .SetRight(Fenrir::Length::Px(5.0f))
        .SetBottom(Fenrir::Length::Px(5.0f))
        .SetWidth(Fenrir::Length::Percent(25.0f))
        .SetHeight(Fenrir::Length::Px(20.0f));
    test.Frame();

    INFO("a quarter of the 200-wide parent, 5 in from its bottom-right corner");
    REQUIRE(UITest::Near(root.GetBoxes(badge).border, {145.0f, 75.0f, 50.0f, 20.0f}));
}

TEST_CASE("Gaps, percentages, minimums and maximums lay out rows as flexbox does", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {1000.0f, 500.0f});
    const Fenrir::ElementId row = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(row, "flex-direction", "row"));
    REQUIRE(root.SetStyleProperty(row, "width", "400px"));
    REQUIRE(root.SetStyleProperty(row, "height", "50%"));
    REQUIRE(root.SetStyleProperty(row, "column-gap", "20px"));
    std::vector<Fenrir::ElementId> cells;
    for (int i = 0; i < 3; ++i)
    {
        cells.push_back(root.Create(row, "panel"));
    }
    REQUIRE(root.SetStyleProperty(cells[0], "flex-grow", "1"));
    REQUIRE(root.SetStyleProperty(cells[0], "max-width", "60px"));
    REQUIRE(root.SetStyleProperty(cells[1], "flex-grow", "1"));
    REQUIRE(root.SetStyleProperty(cells[2], "width", "50px"));
    REQUIRE(root.SetStyleProperty(cells[2], "min-width", "100px"));
    test.Frame();

    INFO("the row is half the 500-high root");
    REQUIRE(UITest::Near(root.GetBoxes(row).border, {0.0f, 0.0f, 400.0f, 250.0f}));
    INFO("the last cell's 50 width is held at its 100 minimum; of the 260 left past two 20-wide gaps, the first "
         "growing cell is capped at 60 and the second takes the other 200");
    REQUIRE(UITest::Near(root.GetBoxes(cells[0]).border, {0.0f, 0.0f, 60.0f, 250.0f}));
    REQUIRE(UITest::Near(root.GetBoxes(cells[1]).border, {80.0f, 0.0f, 200.0f, 250.0f}));
    REQUIRE(UITest::Near(root.GetBoxes(cells[2]).border, {300.0f, 0.0f, 100.0f, 250.0f}));
}

TEST_CASE("The 1080-height rule spans the target's pixel height with 1080 UI units at any size", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {960.0f, 540.0f}, 2.0f, Fenrir::ScaleRule::Height1080);
    const Fenrir::ElementId bar = root.Create(root.GetRootElement(), "panel");
    root.EditInlineStyle(bar)->SetWidth(Fenrir::Length::Px(480.0f)).SetHeight(Fenrir::Length::Px(108.0f));
    test.Frame();

    INFO("540 logical units at 2 pixels each are 1080 pixels, so a UI unit is a pixel, half a logical unit");
    REQUIRE(UITest::Near(root.GetScale(), 0.5f));
    INFO("the root element spans 1080 UI units high and keeps the target's aspect");
    REQUIRE(UITest::Near(root.GetBoxes(root.GetRootElement()).border, {0.0f, 0.0f, 1920.0f, 1080.0f}));

    target->size = {{640.0f, 360.0f}, 1.0f};
    test.Frame();
    INFO("at 360 pixels high a UI unit is a third of a logical unit, and the root still spans 1080");
    REQUIRE(UITest::Near(root.GetScale(), 1.0f / 3.0f));
    REQUIRE(UITest::Near(root.GetBoxes(root.GetRootElement()).border, {0.0f, 0.0f, 1920.0f, 1080.0f}));
    INFO("the bar keeps its UI size, and so its proportion of the target");
    REQUIRE(UITest::Near(root.GetBoxes(bar).border, {0.0f, 0.0f, 480.0f, 108.0f}));
}

TEST_CASE("The content-scale rule makes UI units the target's logical units at any size", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {1280.0f, 720.0f}, 1.5f, Fenrir::ScaleRule::ContentScale);
    test.Frame();
    INFO("one UI unit is one logical unit");
    REQUIRE(UITest::Near(root.GetScale(), 1.0f));
    REQUIRE(UITest::Near(root.GetBoxes(root.GetRootElement()).border, {0.0f, 0.0f, 1280.0f, 720.0f}));

    target->size = {{800.0f, 600.0f}, 1.0f};
    test.Frame();
    INFO("a smaller target is a smaller root");
    REQUIRE(UITest::Near(root.GetBoxes(root.GetRootElement()).border, {0.0f, 0.0f, 800.0f, 600.0f}));
}

TEST_CASE("Pixel snapping rounds each edge to the target's nearest pixel, halves up", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    // Two pixels per UI unit, so whole pixels are half UI units.
    Fenrir::UIRoot& root = test.MakeRoot(target, {500.0f, 500.0f}, 2.0f);
    const Fenrir::ElementId row = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(row, "flex-direction", "row"));
    REQUIRE(root.SetStyleProperty(row, "width", "100px"));
    REQUIRE(root.SetStyleProperty(row, "height", "10px"));
    std::vector<Fenrir::ElementId> thirds;
    for (int i = 0; i < 3; ++i)
    {
        thirds.push_back(root.Create(row, "panel"));
        REQUIRE(root.SetStyleProperty(thirds.back(), "flex-grow", "1"));
    }
    test.Frame();

    // Unsnapped, the thirds' edges are at 0, 33.33, 66.67 and 100 UI units: 66.67, 133.33 pixels.
    INFO("edges land on half UI units, the thirds sharing them so they neither overlap nor leave gaps");
    REQUIRE(UITest::Near(root.GetBoxes(thirds[0]).border, {0.0f, 0.0f, 33.5f, 10.0f}));
    REQUIRE(UITest::Near(root.GetBoxes(thirds[1]).border, {33.5f, 0.0f, 33.0f, 10.0f}));
    REQUIRE(UITest::Near(root.GetBoxes(thirds[2]).border, {66.5f, 0.0f, 33.5f, 10.0f}));

    root.EditInlineStyle(row)->SetLeft(Fenrir::Length::Px(0.25f));
    test.Frame();
    INFO("an edge exactly between two pixels (0.5 pixels) goes to the higher one, the same for every element");
    REQUIRE(UITest::Near(root.GetBoxes(row).border.x, 0.5f));
    REQUIRE(UITest::Near(root.GetBoxes(thirds[0]).border.x, 0.5f));

    Fenrir::UIRootConfig unsnapped;
    unsnapped.snapToPixels = false;
    unsnapped.scaleRule = Fenrir::ScaleRule::ContentScale;
    unsnapped.target = target;
    Fenrir::UIRoot& exact = test.Roots().CreateRoot(unsnapped);
    const Fenrir::ElementId exactRow = exact.Create(exact.GetRootElement(), "panel");
    REQUIRE(exact.SetStyleProperty(exactRow, "flex-direction", "row"));
    REQUIRE(exact.SetStyleProperty(exactRow, "width", "100px"));
    const Fenrir::ElementId first = exact.Create(exactRow, "panel");
    REQUIRE(exact.SetStyleProperty(first, "flex-grow", "1"));
    REQUIRE(exact.SetStyleProperty(exact.Create(exactRow, "panel"), "flex-grow", "2"));
    test.Frame();
    INFO("with snapping off the edges stay where layout put them");
    REQUIRE(UITest::Near(exact.GetBoxes(first).border.width, 100.0f / 3.0f));
}

TEST_CASE("Padding and border boxes nest inside the border box", "[ui][layout]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(panel, "width", "100px"));
    REQUIRE(root.SetStyleProperty(panel, "height", "80px"));
    REQUIRE(root.SetStyleProperty(panel, "margin-left", "10px"));
    REQUIRE(root.SetStyleProperty(panel, "border-left-width", "4px"));
    REQUIRE(root.SetStyleProperty(panel, "border-top-width", "2px"));
    REQUIRE(root.SetStyleProperty(panel, "padding-left", "6px"));
    REQUIRE(root.SetStyleProperty(panel, "padding-top", "3px"));
    test.Frame();

    const Fenrir::ElementBoxes boxes = root.GetBoxes(panel);
    INFO("the border box is the laid-out rectangle, after the margin");
    REQUIRE(UITest::Near(boxes.border, {10.0f, 0.0f, 100.0f, 80.0f}));
    INFO("the padding box is inside the border");
    REQUIRE(UITest::Near(boxes.padding, {14.0f, 2.0f, 96.0f, 78.0f}));
    INFO("the content box is inside the padding");
    REQUIRE(UITest::Near(boxes.content, {20.0f, 5.0f, 90.0f, 75.0f}));
}
