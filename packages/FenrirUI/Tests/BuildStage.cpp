// The build stage (spec: "FenrirUI: the build stage"; ADR-0068) through a capturing target: work added to UIRoots'
// build stage runs as the first work of the UI's frame, so what it makes and changes is styled, laid out and painted in
// the same frame.
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/PainterVertex.hpp"
#include "UITestApp.hpp"

namespace
{
    // Each quad's colour, in the order drawn: every painter box is one quad of four vertices.
    std::vector<std::uint32_t> Colours(const Fenrir::DrawList2D& list)
    {
        std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
        std::vector<std::uint32_t> colours;
        for (std::size_t i = 0; i < vertices.size(); i += 4)
        {
            colours.push_back(vertices[i].colour);
        }
        return colours;
    }

    // Opaque colours as the painter writes them: RGBA8, red in the lowest byte.
    constexpr std::uint32_t Red = 0xFF0000FFu;
    constexpr std::uint32_t Blue = 0xFFFF0000u;
} // namespace

TEST_CASE("An element made in build stage work is laid out and painted the same frame", "[ui][build][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

    Fenrir::ElementId made;
    test.Roots().AddBuildStage(0, [&] {
        if (made.IsValid())
        {
            return;
        }
        made = root.Create(root.GetRootElement(), "panel");
        REQUIRE(root.SetStyleProperty(made, "background-color", "#f00"));
        REQUIRE(root.SetStyleProperty(made, "width", "120px"));
        REQUIRE(root.SetStyleProperty(made, "height", "40px"));
    });
    Fenrir::Rect2D seenInPaint;
    test.Roots().AddPaintStage(0, [&] { seenInPaint = root.GetBoxes(made).border; });
    test.Frame();

    INFO("The element exists after the first frame");
    REQUIRE(root.IsAlive(made));
    INFO("Laid out in the frame that made it");
    REQUIRE(UITest::Near(root.GetBoxes(made).border, {0.0f, 0.0f, 120.0f, 40.0f}));
    INFO("The paint stage of that frame already saw its rectangle");
    REQUIRE(UITest::Near(seenInPaint, {0.0f, 0.0f, 120.0f, 40.0f}));
    INFO("And it is in that frame's draw list");
    REQUIRE(Colours(target->lists.back()) == std::vector<std::uint32_t>{Red});
}

TEST_CASE("A style changed in build stage work is painted the same frame", "[ui][build][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(panel, "background-color", "#f00"));
    REQUIRE(root.SetStyleProperty(panel, "width", "10px"));
    REQUIRE(root.SetStyleProperty(panel, "height", "10px"));
    test.Frame();
    REQUIRE(Colours(target->lists.back()) == std::vector<std::uint32_t>{Red});

    test.Roots().AddBuildStage(0, [&] { REQUIRE(root.SetStyleProperty(panel, "background-color", "#00f")); });
    test.Frame();
    INFO("The new colour, not last frame's");
    REQUIRE(Colours(target->lists.back()) == std::vector<std::uint32_t>{Blue});
}

TEST_CASE("Build stage work runs lowest order first, equal orders in the order added", "[ui][build][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    test.MakeRoot(target, {800.0f, 600.0f});
    std::vector<int> ran;
    test.Roots().AddPaintStage(-100, [&] { ran.push_back(0); });
    test.Roots().AddBuildStage(10, [&] { ran.push_back(10); });
    test.Roots().AddBuildStage(-5, [&] { ran.push_back(-5); });
    test.Roots().AddBuildStage(10, [&] { ran.push_back(11); });
    test.Frame();
    INFO("-5, then the two at 10 as added, all before the paint stage");
    REQUIRE(ran == std::vector<int>{-5, 10, 11, 0});
}

TEST_CASE("Build stage work that is removed runs no more", "[ui][build][stage]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    test.MakeRoot(target, {800.0f, 600.0f});
    int runs = 0;
    const Fenrir::BuildStageHandle work = test.Roots().AddBuildStage(0, [&] { ++runs; });
    test.Frame();
    INFO("Removing it the first time takes it out");
    REQUIRE(test.Roots().RemoveBuildStage(work));
    INFO("A second removal names nothing, and the empty handle names none");
    REQUIRE_FALSE(test.Roots().RemoveBuildStage(work));
    REQUIRE_FALSE(test.Roots().RemoveBuildStage(Fenrir::BuildStageHandle{}));
    test.Frame();
    INFO("It ran once, before it was removed");
    REQUIRE(runs == 1);
}

TEST_CASE("Build stage work runs once a frame with no root", "[ui][build][stage]")
{
    UITest::UITestApp test;
    int runs = 0;
    test.Roots().AddBuildStage(0, [&] { ++runs; });
    test.Frame();
    INFO("The stage runs once a frame whether or not a root exists");
    REQUIRE(runs == 1);
}
