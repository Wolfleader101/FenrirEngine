// World roots (ADR-0060, ADR-0066; spec: "FenrirUI" root targets and input, seam 1): a root whose target is a
// UIWorldTarget lays out over its entity's WorldQuad2D and takes its pointer from the ray the game gives it each frame,
// which it meets on the quad and hit tests in UI units; it takes no pointer from the window, and gamepad navigation
// works on it as on any root.

#include <algorithm>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UIWorldTarget.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIRoot;
using Fenrir::Math::Ray;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using namespace UITest;

namespace
{
    constexpr Fenrir::GamepadId testPad{3};

    // A headless app with one 2 by 1 world unit quad at the origin, facing +Z, 100 UI units to a world unit, and a
    // root on it: 200 by 100 UI units.
    struct WorldTest
    {
        UITestApp test;
        Fenrir::Entity quad;

        WorldTest()
        {
            quad = test.app.GetActiveScene().GetEntityList().CreateEntity();
            quad.AddComponent<Fenrir::WorldQuad2D>(Fenrir::WorldQuad2D{{2.0f, 1.0f}, 100.0f});
        }

        // A root on the quad, without the default stylesheet, so its elements are exactly the size they are given.
        UIRoot& MakeRoot()
        {
            Fenrir::UIRootConfig config;
            config.target = std::make_shared<Fenrir::UIWorldTarget>(test.app, quad);
            config.defaultStylesheet = false;
            return test.Roots().CreateRoot(config);
        }

        // An element of `tag` placed absolutely at `x`, `y`, sized `width` by `height`, taking pointer input.
        static ElementId Place(UIRoot& root, std::string_view tag, float x, float y, float width, float height)
        {
            const ElementId element = root.Create(root.GetRootElement(), tag);
            INFO("the element's inline style did not take its place and size");
            REQUIRE(root.SetStyleProperty(element, "position", "absolute"));
            REQUIRE(root.SetStyleProperty(element, "left", std::to_string(x) + "px"));
            REQUIRE(root.SetStyleProperty(element, "top", std::to_string(y) + "px"));
            REQUIRE(root.SetStyleProperty(element, "width", std::to_string(width) + "px"));
            REQUIRE(root.SetStyleProperty(element, "height", std::to_string(height) + "px"));
            REQUIRE(root.SetStyleProperty(element, "pointer-events", "all"));
            return element;
        }
    };

    // A ray from 5 units in front of the quad, at `x`, `y` in the quad's plane, straight at it.
    Ray Towards(float x, float y)
    {
        Ray ray;
        ray.origin = Vec3(x, y, 5.0f);
        ray.dir = Vec3(0.0f, 0.0f, -1.0f);
        return ray;
    }

    // How many UIEvents of `kind` at `target` `reader` has not read yet.
    std::size_t CountAt(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app, UIEventKind kind, ElementId target)
    {
        std::size_t count = 0;
        for (const UIEvent& event : reader.Read(app))
        {
            count += event.kind == kind && event.target == target ? 1 : 0;
        }
        return count;
    }
} // namespace

TEST_CASE("A world root lays out over its quad's size in UI units, at a scale of 1, blending linear", "[ui][world]")
{
    WorldTest world;
    UIRoot& root = world.MakeRoot();
    world.test.Frame();

    INFO("the quad's 2 by 1 world units, 100 UI units each: the root element is 200 by 100");
    REQUIRE(Near(root.GetBoxes(root.GetRootElement()).border, Fenrir::Rect2D{0.0f, 0.0f, 200.0f, 100.0f}));
    INFO("UI units are the quad's logical units, and it blends as the lit scene does");
    REQUIRE(root.GetConfig().scaleRule == Fenrir::ScaleRule::ContentScale);
    REQUIRE(root.GetConfig().blendSpace == Fenrir::BlendSpace2D::Linear);
}

TEST_CASE("A ray through a world quad's centre hovers and clicks the element there", "[ui][world]")
{
    WorldTest world;
    UIRoot& root = world.MakeRoot();
    const ElementId centre = WorldTest::Place(root, "panel", 75.0f, 25.0f, 50.0f, 50.0f);
    world.test.Frame();
    Fenrir::EventReader<UIEvent> events;

    INFO("the ray meets the quad at its centre, (100, 50) in UI units, over the element");
    REQUIRE(root.SetPointerRay(Towards(0.0f, 0.0f), false));
    REQUIRE(root.GetHovered() == centre);
    REQUIRE(root.GetPointer().has_value());
    REQUIRE(Near(root.GetPointer()->x, 100.0f));
    REQUIRE(Near(root.GetPointer()->y, 50.0f));

    world.test.Frame();
    (void)root.SetPointerRay(Towards(0.0f, 0.0f), true);
    world.test.Frame();
    (void)root.SetPointerRay(Towards(0.0f, 0.0f), false);
    world.test.Frame();
    INFO("pressed and let go there: one click on the element");
    REQUIRE(CountAt(events, world.test.app, UIEventKind::Click, centre) == 1);
}

TEST_CASE("A ray that misses a world quad clears the hover, as does no ray", "[ui][world]")
{
    WorldTest world;
    UIRoot& root = world.MakeRoot();
    const ElementId panel = WorldTest::Place(root, "panel", 0.0f, 0.0f, 200.0f, 100.0f);
    world.test.Frame();

    INFO("a ray at the quad hovers the panel over it");
    REQUIRE(root.SetPointerRay(Towards(0.5f, 0.25f), false));
    REQUIRE(root.GetHovered() == panel);

    INFO("a ray past the quad's right edge hovers nothing, and the root has no pointer");
    REQUIRE_FALSE(root.SetPointerRay(Towards(1.5f, 0.0f), false));
    REQUIRE_FALSE(root.GetHovered().IsValid());
    REQUIRE_FALSE(root.GetPointer().has_value());

    REQUIRE(root.SetPointerRay(Towards(0.0f, 0.0f), false));
    INFO("a ray pointing away from the quad misses it");
    Ray away = Towards(0.0f, 0.0f);
    away.dir = Vec3(0.0f, 0.0f, 1.0f);
    REQUIRE_FALSE(root.SetPointerRay(away, false));
    REQUIRE_FALSE(root.GetHovered().IsValid());

    REQUIRE(root.SetPointerRay(Towards(0.0f, 0.0f), false));
    INFO("no ray at all clears the hover too");
    REQUIRE_FALSE(root.SetPointerRay(std::nullopt, false));
    REQUIRE_FALSE(root.GetHovered().IsValid());
}

TEST_CASE("A turned world quad maps the ray's hit through its rotation", "[ui][world]")
{
    WorldTest world;
    // A quarter turn about Y: the quad faces +X, its local +X along world -Z.
    world.quad.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(std::numbers::pi_v<float> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    UIRoot& root = world.MakeRoot();
    const ElementId left = WorldTest::Place(root, "panel", 0.0f, 0.0f, 100.0f, 100.0f);
    const ElementId right = WorldTest::Place(root, "panel", 100.0f, 0.0f, 100.0f, 100.0f);
    world.test.Frame();

    // From +X, along -X, half a unit towards -Z: half a unit along the quad's own X, so 150 UI units across.
    Ray ray;
    ray.origin = Vec3(5.0f, 0.25f, -0.5f);
    ray.dir = Vec3(-1.0f, 0.0f, 0.0f);
    INFO("the hit is on the quad's right half, a quarter unit up: (150, 25)");
    REQUIRE(root.SetPointerRay(ray, false));
    REQUIRE(root.GetHovered() == right);
    REQUIRE(Near(root.GetPointer()->x, 150.0f, 1e-2f));
    REQUIRE(Near(root.GetPointer()->y, 25.0f, 1e-2f));

    ray.origin.z = 0.5f;
    REQUIRE(root.SetPointerRay(ray, false));
    INFO("half a unit towards +Z is the quad's left half");
    REQUIRE(root.GetHovered() == left);

    INFO("along the turned quad's plane, the ray never meets it");
    ray.dir = Vec3(0.0f, 0.0f, -1.0f);
    ray.origin = Vec3(0.0f, 0.0f, 5.0f);
    REQUIRE_FALSE(root.SetPointerRay(ray, false));
}

TEST_CASE("A press held on a world root keeps its element while the ray leaves the quad", "[ui][world]")
{
    WorldTest world;
    UIRoot& root = world.MakeRoot();
    const ElementId panel = WorldTest::Place(root, "panel", 0.0f, 0.0f, 100.0f, 100.0f);
    world.test.Frame();
    Fenrir::EventReader<UIEvent> events;

    // Events are read as each frame sends them: a reader sees only the last frames' events.
    std::vector<UIEvent> read;
    const auto frame = [&] {
        world.test.Frame();
        const std::vector<UIEvent> sent = ReadAll(events, world.test.app);
        read.insert(read.end(), sent.begin(), sent.end());
    };
    (void)root.SetPointerRay(Towards(-0.5f, 0.0f), true);
    frame();
    INFO("held past the quad's left edge, the drag goes on: the pointer is in the quad's plane outside it");
    REQUIRE(root.SetPointerRay(Towards(-1.5f, 0.0f), true));
    REQUIRE(root.GetPointer().has_value());
    REQUIRE(Near(root.GetPointer()->x, -50.0f, 1e-2f));
    (void)root.SetPointerRay(Towards(-1.5f, 0.0f), false);
    frame();
    INFO("let go outside it: released, and no click");
    const auto count = [&](UIEventKind kind) {
        return std::count_if(read.begin(), read.end(),
                             [&](const UIEvent& event) { return event.kind == kind && event.target == panel; });
    };
    REQUIRE(count(UIEventKind::PointerPress) == 1);
    REQUIRE(count(UIEventKind::PointerRelease) == 1);
    REQUIRE(count(UIEventKind::Click) == 0);
}

TEST_CASE("A press held on a world root when the ray goes away is cancelled, with no click", "[ui][world]")
{
    WorldTest world;
    UIRoot& root = world.MakeRoot();
    const ElementId panel = WorldTest::Place(root, "panel", 0.0f, 0.0f, 200.0f, 100.0f);
    world.test.Frame();
    Fenrir::EventReader<UIEvent> events;
    std::vector<UIEvent> read;
    const auto frame = [&] {
        world.test.Frame();
        const std::vector<UIEvent> sent = ReadAll(events, world.test.app);
        read.insert(read.end(), sent.begin(), sent.end());
    };

    (void)root.SetPointerRay(Towards(0.0f, 0.0f), true);
    frame();
    INFO("no ray while the button is still down: the press is let go of");
    REQUIRE_FALSE(root.SetPointerRay(std::nullopt, true));
    frame();
    INFO("back on the element with the button let go: nothing presses or clicks");
    (void)root.SetPointerRay(Towards(0.0f, 0.0f), false);
    frame();
    const auto count = [&](UIEventKind kind) {
        return std::count_if(read.begin(), read.end(),
                             [&](const UIEvent& event) { return event.kind == kind && event.target == panel; });
    };
    REQUIRE(count(UIEventKind::PointerPress) == 1);
    REQUIRE(count(UIEventKind::PointerRelease) == 1);
    REQUIRE(count(UIEventKind::Click) == 0);
}

TEST_CASE("A world root takes no pointer from the window", "[ui][world]")
{
    WorldTest world;
    UIRoot& root = world.MakeRoot();
    const ElementId panel = WorldTest::Place(root, "panel", 0.0f, 0.0f, 200.0f, 100.0f);
    world.test.Frame();
    Fenrir::EventReader<UIEvent> events;

    world.test.Click({50.0f, 50.0f});
    world.test.Frame();
    INFO("the window's mouse over the root's units presses nothing and hovers nothing");
    REQUIRE_FALSE(root.GetHovered().IsValid());
    REQUIRE(CountAt(events, world.test.app, UIEventKind::PointerPress, panel) == 0);
    INFO("and the game keeps it: the UI claims no pointer");
    REQUIRE_FALSE(world.test.app.GetResource<Fenrir::InputClaims>().For(testWindow).pointer);
}

TEST_CASE("A focused world root takes gamepad navigation and publishes the gamepad claim", "[ui][world][gamepad]")
{
    WorldTest world;
    world.test.Feed().GamepadConnected(testPad);
    UIRoot& root = world.MakeRoot();
    const ElementId first = WorldTest::Place(root, "button", 10.0f, 10.0f, 50.0f, 30.0f);
    const ElementId second = WorldTest::Place(root, "button", 100.0f, 10.0f, 50.0f, 30.0f);
    world.test.Frame();
    INFO("nothing focused: no claim");
    REQUIRE_FALSE(world.test.app.GetResource<Fenrir::InputClaims>().Gamepad());

    // Clicked by the ray, the first button takes the focus.
    (void)root.SetPointerRay(Towards(-0.65f, 0.25f), true);
    world.test.Frame();
    (void)root.SetPointerRay(Towards(-0.65f, 0.25f), false);
    world.test.Frame();
    REQUIRE(root.GetFocused() == first);
    INFO("focused, the world root claims the gamepad");
    REQUIRE(world.test.app.GetResource<Fenrir::InputClaims>().Gamepad());

    world.test.Feed().GamepadButtonDown(testPad, Fenrir::GamepadButton::DPadRight);
    world.test.Feed().GamepadButtonUp(testPad, Fenrir::GamepadButton::DPadRight);
    world.test.Frame();
    INFO("the D-pad moves the focus to the button on the right");
    REQUIRE(root.GetFocused() == second);
}
