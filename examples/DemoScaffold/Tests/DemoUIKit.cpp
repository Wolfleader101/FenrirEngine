// The demo UI kit (ADR-0071; spec: .scratch/demo-ui/spec.md): its root over the scene, the demo panel's sections, the
// help's key rows, F1, and which input reaches the scene, on the null backend with the kit's root drawing into a
// capturing target the size of the null backend's surface.

#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/UIRoot.hpp"

using namespace DemoScaffoldTests;
using FenrirDemo::DemoKeyRow;

namespace
{
    constexpr Fenrir::WindowId mainWindow{1};
    constexpr Fenrir::GamepadId testPad{3};
    // Left of the window's centre, clear of the panel on the right and the help at the bottom left.
    const Fenrir::Math::Vec2 besidePanels{500.0f, 200.0f};

    // A scene whose spawn adds a scene section titled `section` holding one line.
    FenrirDemo::DemoScene SceneWithSection(std::string name, std::string section)
    {
        FenrirDemo::DemoScene scene = EmptyScene(std::move(name));
        scene.spawn = [section](Fenrir::App& app, Fenrir::Scene&) {
            app.GetResource<FenrirDemo::DemoUI>().SceneSection(section).Text("Lights: 4");
        };
        return scene;
    }
} // namespace

TEST_CASE("The help lists the app and the scene, then the scaffold's, the app's and the scene's key rows",
          "[demo][kit][help]")
{
    FenrirDemo::DemoScene scene = EmptyScene("Lit cube");
    scene.keyRows = {{"L", "rotate the light"}, {"K", "change its colour"}};
    DemoTestApp test({scene, EmptyScene("Other")}, NullRun(), {{"F2", "cycle the debug lines"}});
    test.Frames(2);

    INFO("the help's heading names the app and the scene, i of n");
    REQUIRE(test.Texts("demo-help-heading") == std::vector<std::string>{"TestDemo: Lit cube (1/2)"});
    const std::vector<std::string> keys = test.Texts("demo-keys");
    const std::vector<std::string> expected{
        "PageDown/PageUp", "F1", "F3", "Right mouse", "Right mouse + WASD", "Left click", "F2", "L", "K"};
    INFO("the scaffold's rows, then the app's, then the scene's, in order");
    REQUIRE(keys == expected);
    const std::vector<std::string> what = test.Texts("demo-what");
    INFO("each row says what its keys do");
    REQUIRE(what.size() == expected.size());
    INFO("the app's row says what F2 does");
    REQUIRE(what[6] == "cycle the debug lines");
    INFO("the scene's first row says what L does");
    REQUIRE(what[7] == "rotate the light");
}

TEST_CASE("F1 hides the kit, whose panels then take no press, and shows it again", "[demo][kit][f1]")
{
    DemoTestApp test({EmptyScene("Open space")});
    test.Frames(2);
    const Fenrir::Math::Vec2 panel = test.Centre(test.Kit().Root().FindById("demo-panel"));

    test.Tap(Fenrir::Key::F1);
    INFO("F1 hides the kit");
    REQUIRE_FALSE(test.Kit().IsVisible());
    INFO("where the panel was, the press is the scene's");
    REQUIRE(test.SceneClicksAt(panel) == 1);

    test.Tap(Fenrir::Key::F1);
    INFO("F1 again shows it");
    REQUIRE(test.Kit().IsVisible());
    INFO("and the panel takes the press again");
    REQUIRE(test.SceneClicksAt(panel) == 0);
}

TEST_CASE("F1 hides the kit while one of its buttons has the focus", "[demo][kit][f1]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);
    Fenrir::UIRoot& root = test.Kit().Root();
    const std::vector<Fenrir::ElementId> headings = root.FindByClass("demo-section-title");
    INFO("the scene's section has a heading button");
    REQUIRE_FALSE(headings.empty());
    INFO("which takes the focus");
    REQUIRE(root.Focus(headings.front()));
    test.Frames();

    test.Tap(Fenrir::Key::F1);
    INFO("the focused kit has the keyboard, and still F1 hides it");
    REQUIRE_FALSE(test.Kit().IsVisible());
    INFO("hidden, the kit has given up the focus, so the scene has the keys again");
    REQUIRE_FALSE(root.GetFocused().IsValid());
}

TEST_CASE("A press on the demo panel sends no scene click, and one beside it does", "[demo][kit][click]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);

    INFO("the panel takes the press");
    REQUIRE(test.SceneClicksAt(test.Centre(test.Kit().Root().FindById("demo-panel"))) == 0);
    INFO("beside the panels the press is the scene's");
    REQUIRE(test.SceneClicksAt(besidePanels) == 1);
}

TEST_CASE("The pointer over the demo panel is claimed, and beside it is not", "[demo][kit][click]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);

    test.Feed().MouseMove(mainWindow, test.Centre(test.Kit().Root().FindById("demo-panel")), {0.0f, 0.0f});
    test.Frames();
    INFO("over the panel the kit claims the pointer");
    REQUIRE(test.Claims().pointer);

    test.Feed().MouseMove(mainWindow, besidePanels, {0.0f, 0.0f});
    test.Frames();
    INFO("beside the panels the pointer claim is down");
    REQUIRE_FALSE(test.Claims().pointer);
}

TEST_CASE("A press on the demo panel sends the game no mouse button, and one beside it does", "[demo][kit][click]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);
    SentEvents<Fenrir::MouseButtonEvent> buttons(test);

    test.Click(test.Centre(test.Kit().Root().FindById("demo-panel")));
    INFO("the game saw the button go neither down nor up on the panel");
    REQUIRE(buttons.Events().empty());

    test.Click(besidePanels);
    INFO("beside the panels it saw both");
    REQUIRE(buttons.Events().size() == 2);
}

TEST_CASE("A right-drag beside the demo panel still turns the free-fly camera", "[demo][kit][camera]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);
    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();
    const Fenrir::Math::Quat before = test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot;

    feed.MouseMove(mainWindow, besidePanels, {0.0f, 0.0f});
    test.Frames();
    feed.MouseButtonDown(mainWindow, Fenrir::MouseButton::Right);
    test.Frames();
    feed.MouseMove(mainWindow, besidePanels + Fenrir::Math::Vec2(100.0f, 0.0f), {100.0f, 0.0f});
    test.Frames();
    feed.MouseButtonUp(mainWindow, Fenrir::MouseButton::Right);
    test.Frames();

    const Fenrir::Math::Quat after = test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot;
    INFO("the drag reached the free-fly camera, which turned");
    REQUIRE_FALSE(after == before);
}

TEST_CASE("A scene's section is gone once the scene is left", "[demo][kit][scenes]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights"), EmptyScene("Empty")});
    test.Frames(2);
    INFO("the scene's section is in the panel, after the app's scene switcher");
    REQUIRE(test.Texts("demo-section-title") == std::vector<std::string>{"Scenes", "Lights"});

    test.Tap(Fenrir::Key::PageDown);
    INFO("after PageDown the next scene has no section, and the last one's is gone");
    REQUIRE(test.Texts("demo-section-title") == std::vector<std::string>{"Scenes"});
}

TEST_CASE("A section's heading collapses and opens it", "[demo][kit][sections]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);
    Fenrir::UIRoot& root = test.Kit().Root();
    const Fenrir::ElementId section = root.FindByClass("demo-section").front();
    const Fenrir::ElementId title = root.FindByClass("demo-section-title").front();
    const float open = root.GetBoxes(section).border.height;
    const float heading = root.GetBoxes(title).border.height;

    test.Click(test.Centre(title));
    INFO("a click on the heading collapses the section to little more than its heading");
    const float collapsed = root.GetBoxes(section).border.height;
    REQUIRE(collapsed < open);
    REQUIRE(collapsed >= heading);

    test.Click(test.Centre(title));
    INFO("a second click opens it again");
    REQUIRE(root.GetBoxes(section).border.height == open);
}

TEST_CASE("The gamepad reaches the scene while the kit has the focus", "[demo][kit][gamepad]")
{
    DemoTestApp test({SceneWithSection("Lit cube", "Lights")});
    test.Frames(2);
    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();
    feed.GamepadConnected(testPad);
    Fenrir::UIRoot& root = test.Kit().Root();
    INFO("the section's heading takes the focus");
    REQUIRE(root.Focus(root.FindByClass("demo-section-title").front()));
    test.Frames();

    feed.GamepadButtonDown(testPad, Fenrir::GamepadButton::DPadDown);
    feed.GamepadButtonDown(testPad, Fenrir::GamepadButton::South);
    test.Frames();
    const Fenrir::Gamepads& pads = test.app.GetResource<Fenrir::Gamepads>();
    INFO("the D-pad and A are the scene's");
    REQUIRE(pads.IsDown(testPad, Fenrir::GamepadButton::DPadDown));
    REQUIRE(pads.IsDown(testPad, Fenrir::GamepadButton::South));
    INFO("and the kit claims no gamepad");
    REQUIRE_FALSE(test.app.GetResource<Fenrir::InputClaims>().Gamepad());
}
