// The paint stage (spec: "Scripting"; ADR-0039, ADR-0061) through a capturing target: work added to
// UIRoots' paint stage paints into elements through UIRoot::PaintInto after layout, and what it painted is in the
// root's list after the element's own boxes and before its children, under the element's transform and inside the clips
// the element's own content is drawn in.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "UITestApp.hpp"

namespace
{
    // Each quad's first vertex, in the order drawn: every painter box is one quad of four.
    std::vector<Fenrir::PainterVertex> Quads(const Fenrir::DrawList2D& list)
    {
        std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
        std::vector<Fenrir::PainterVertex> quads;
        for (std::size_t i = 0; i < vertices.size(); i += 4)
        {
            quads.push_back(vertices[i]);
        }
        return quads;
    }

    // Every quad's colour, in the order drawn.
    std::vector<std::uint32_t> Colours(const Fenrir::DrawList2D& list)
    {
        std::vector<std::uint32_t> colours;
        for (const Fenrir::PainterVertex& quad : Quads(list))
        {
            colours.push_back(quad.colour);
        }
        return colours;
    }

    bool IsClipped(const Fenrir::PainterVertex& vertex)
    {
        return (vertex.kinds[2] & Fenrir::PainterFlags::Clipped) != 0;
    }

    // Opaque colours as the painter writes them: RGBA8, red in the lowest byte.
    constexpr std::uint32_t Red = 0xFF0000FFu;
    constexpr std::uint32_t Green = 0xFF00FF00u;
    constexpr std::uint32_t Blue = 0xFFFF0000u;
    constexpr std::uint32_t White = 0xFFFFFFFFu;

    // A panel under `parent` of `width` by `height` with a background of `colour`, as CSS writes it.
    Fenrir::ElementId Panel(Fenrir::UIRoot& root, Fenrir::ElementId parent, const char* colour, const char* width,
                            const char* height)
    {
        const Fenrir::ElementId panel = root.Create(parent, "panel");
        REQUIRE(root.SetStyleProperty(panel, "background-color", colour));
        REQUIRE(root.SetStyleProperty(panel, "width", width));
        REQUIRE(root.SetStyleProperty(panel, "height", height));
        return panel;
    }
} // namespace

TEST_CASE("Paint stage work paints into an element after its own box and before its children", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId parent = Panel(root, root.GetRootElement(), "#f00", "200px", "100px");
    REQUIRE(root.SetStyleProperty(parent, "padding", "10px"));
    Panel(root, parent, "#00f", "50px", "50px");

    Fenrir::Rect2D seen;
    test.Roots().AddPaintStage(0, [&] {
        Fenrir::Painter* painter = root.PaintInto(parent);
        REQUIRE(painter != nullptr);
        // Laid out already: the stage runs after layout.
        seen = root.GetBoxes(parent).border;
        painter->DrawRect({seen.x + 20.0f, seen.y + 20.0f, 10.0f, 10.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f});
    });
    test.Frame();

    INFO("The stage saw the element's laid-out border box");
    REQUIRE(UITest::Near(seen, {0.0f, 0.0f, 200.0f, 100.0f}));
    INFO("The parent's box, the stage's rectangle, then the child's box");
    REQUIRE(Colours(target->lists.back()) == std::vector<std::uint32_t>{Red, Green, Blue});
    INFO("The stage's rectangle is where it drew it, in the root's UI units");
    const Fenrir::PainterVertex drawn = Quads(target->lists.back())[1];
    REQUIRE(UITest::Near(drawn.halfSize.x, 5.0f));

    test.Frame();
    INFO("It paints again each frame the stage runs");
    REQUIRE(Colours(target->lists.back()) == std::vector<std::uint32_t>{Red, Green, Blue});
}

TEST_CASE("A paint stage painter draws under the element's transform and inside the clips its content is drawn in",
          "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId clipper = Panel(root, root.GetRootElement(), "#f00", "100px", "100px");
    REQUIRE(root.SetStyleProperty(clipper, "overflow", "hidden"));
    const Fenrir::ElementId moved = Panel(root, clipper, "#00f", "40px", "40px");
    REQUIRE(root.SetStyleProperty(moved, "transform", "translate(30px, 0px)"));

    test.Roots().AddPaintStage(0, [&] {
        if (Fenrir::Painter* painter = root.PaintInto(moved))
        {
            painter->DrawRect({0.0f, 0.0f, 10.0f, 10.0f}, Fenrir::Colour{1.0f, 1.0f, 1.0f, 1.0f});
        }
    });
    test.Frame();

    const std::vector<Fenrir::PainterVertex> quads = Quads(target->lists.back());
    INFO("The clipper's box, the moved element's, then the stage's rectangle");
    REQUIRE(Colours(target->lists.back()) == std::vector<std::uint32_t>{Red, Blue, White});
    INFO("Moved by the element's transform as its own box is");
    REQUIRE(UITest::Near(quads[2].position.x - quads[1].position.x, 0.0f));
    INFO("Clipped by its ancestor's overflow, as the element's own box is");
    REQUIRE(IsClipped(quads[1]));
    REQUIRE(IsClipped(quads[2]));
}

TEST_CASE("Outside the paint stage PaintInto gives no painter", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = Panel(root, root.GetRootElement(), "#f00", "10px", "10px");
    test.Frame();
    INFO("After the frame, the stage is over");
    REQUIRE(root.PaintInto(panel) == nullptr);
}

TEST_CASE("In the paint stage a stale element gets no painter", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId removed = Panel(root, root.GetRootElement(), "#f00", "10px", "10px");
    REQUIRE(root.Remove(removed, Fenrir::RemoveMode::Immediate));
    bool stale = false;
    test.Roots().AddPaintStage(0, [&] { stale = root.PaintInto(removed) == nullptr; });
    test.Frame();
    INFO("The removed element's id is stale");
    REQUIRE(stale);
}

TEST_CASE("Calls for one element in one paint stage give one painter", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = Panel(root, root.GetRootElement(), "#f00", "10px", "10px");
    bool same = false;
    test.Roots().AddPaintStage(0, [&] { same = root.PaintInto(panel) == root.PaintInto(panel); });
    test.Frame();
    INFO("The second call gives the first's painter");
    REQUIRE(same);
}

TEST_CASE("Paint stage work runs lowest order first, equal orders in the order added", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    test.MakeRoot(target, {800.0f, 600.0f});
    std::vector<int> ran;
    test.Roots().AddPaintStage(10, [&] { ran.push_back(10); });
    test.Roots().AddPaintStage(-5, [&] { ran.push_back(-5); });
    test.Roots().AddPaintStage(10, [&] { ran.push_back(11); });
    test.Frame();
    INFO("-5, then the two at 10 as added");
    REQUIRE(ran == std::vector<int>{-5, 10, 11});
}

TEST_CASE("Paint stage work that is removed runs no more", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    test.MakeRoot(target, {800.0f, 600.0f});
    int runs = 0;
    const Fenrir::PaintStageHandle work = test.Roots().AddPaintStage(0, [&] { ++runs; });
    test.Frame();
    INFO("Removing it the first time takes it out");
    REQUIRE(test.Roots().RemovePaintStage(work));
    INFO("A second removal names nothing");
    REQUIRE_FALSE(test.Roots().RemovePaintStage(work));
    test.Frame();
    INFO("It ran once, before it was removed");
    REQUIRE(runs == 1);
}

TEST_CASE("What the paint stage paints fades with an element whose opacity fades it box by box", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    // A leaf: with no children its opacity fades its boxes rather than going through an offscreen group.
    const Fenrir::ElementId faded = Panel(root, root.GetRootElement(), "#f00", "100px", "100px");
    REQUIRE(root.SetStyleProperty(faded, "opacity", "0.5"));
    test.Roots().AddPaintStage(0, [&] {
        if (Fenrir::Painter* painter = root.PaintInto(faded))
        {
            painter->DrawRect({10.0f, 10.0f, 10.0f, 10.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f});
        }
    });
    test.Frame();

    const std::vector<std::uint32_t> colours = Colours(target->lists.back());
    REQUIRE(colours.size() == 2);
    INFO("The element's box and the stage's rectangle are both at half alpha");
    REQUIRE((colours[0] >> 24u) == (colours[1] >> 24u));
    REQUIRE((colours[1] >> 24u) < 0xFFu);
}

TEST_CASE("What the paint stage paints is dropped when it leaves a group open", "[ui][paint][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = Panel(root, root.GetRootElement(), "#f00", "100px", "100px");
    Panel(root, panel, "#00f", "10px", "10px");
    test.Roots().AddPaintStage(0, [&] {
        if (Fenrir::Painter* painter = root.PaintInto(panel))
        {
            painter->BeginGroup({0.0f, 0.0f, 100.0f, 100.0f}, 0.5f);
            painter->DrawRect({10.0f, 10.0f, 10.0f, 10.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f});
        }
    });
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    INFO("The element's box and its child's, and none of the stage's");
    REQUIRE(Colours(list) == std::vector<std::uint32_t>{Red, Blue});
    INFO("The root's list opens no group, so it stays balanced");
    REQUIRE(std::none_of(list.commands.begin(), list.commands.end(), [](const Fenrir::Command2D& command) {
        return std::holds_alternative<Fenrir::BeginGroup2D>(command);
    }));
}
