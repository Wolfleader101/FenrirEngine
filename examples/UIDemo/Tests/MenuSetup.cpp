#include <string>

#include <catch2/catch_test_macros.hpp>

#include "DemoApp.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIRoot.hpp"

// UIDemo's pause menu set up as the app sets it up (DemoApp.hpp), driven a frame at a time through FenrirInput's feed.
namespace
{
    using UIDemoTest::DemoApp;

    constexpr Fenrir::GamepadId Pad{3};
    // The middle of the 1920 by 1080 targets.
    const Fenrir::Math::Vec2 Middle{960.0f, 540.0f};
    // More than the panels' intro and outro last.
    constexpr int OutroFrames = 40;
    constexpr int IntroFrames = 30;

    void TapButton(DemoApp& demo, Fenrir::GamepadButton button)
    {
        demo.Feed().GamepadButtonDown(Pad, button);
        demo.Feed().GamepadButtonUp(Pad, button);
    }

    // Enters the room, presses Escape and runs the frames that lay the menu out.
    void OpenMenu(DemoApp& demo)
    {
        demo.Frames(2);
        demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
        demo.Frames(2);
    }
} // namespace

TEST_CASE("Escape opens the pause menu, whose root then takes pointer input from the game")
{
    DemoApp demo;
    demo.Frames(2);
    UIDemo::Menu& menu = demo.Menu();
    REQUIRE_FALSE(menu.IsOpen());
    INFO("the closed menu has an element under the middle of the window");
    REQUIRE_FALSE(menu.Root().HitTest(Middle).IsValid());

    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frames(2);
    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    REQUIRE(demo.errors->empty());
    REQUIRE(menu.IsOpen());
    INFO("the open menu has no element under the middle of the window");
    REQUIRE(menu.Root().HitTest(Middle).IsValid());

    // The free-fly camera turns while the game's Mouse holds the right button; the menu keeps it.
    demo.Feed().MouseMove(UIDemoTest::Window, {200.0f, 200.0f}, {0.0f, 0.0f});
    demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Right);
    demo.Frame();
    INFO("a right press with the menu open reached the game's Mouse");
    REQUIRE_FALSE(demo.app.GetResource<Fenrir::Mouse>().IsDown(Fenrir::MouseButton::Right));
}

TEST_CASE("A gamepad direction and A click Resume, which closes the menu")
{
    DemoApp demo;
    demo.Feed().GamepadConnected(Pad);
    OpenMenu(demo);
    UIDemo::Menu& menu = demo.Menu();
    REQUIRE(menu.IsOpen());
    Fenrir::UIRoot& root = menu.Root();

    TapButton(demo, Fenrir::GamepadButton::DPadDown);
    demo.Frame();
    INFO("the first direction did not give Resume the focus");
    REQUIRE(root.GetFocused() == root.FindById("resume"));

    TapButton(demo, Fenrir::GamepadButton::South);
    demo.Frame();
    REQUIRE_FALSE(menu.IsOpen());

    demo.Frames(OutroFrames);
    INFO("the menu's elements are still there once its outro has ended");
    REQUIRE_FALSE(root.HitTest(Middle).IsValid());
    // With the menu closed the game has the right button again.
    demo.Feed().MouseMove(UIDemoTest::Window, {200.0f, 200.0f}, {0.0f, 0.0f});
    demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Right);
    demo.Frame();
    REQUIRE(demo.app.GetResource<Fenrir::Mouse>().IsDown(Fenrir::MouseButton::Right));
}

TEST_CASE("The name typed into the settings panel shows on the HUD")
{
    DemoApp demo;
    OpenMenu(demo);
    UIDemo::Menu& menu = demo.Menu();
    Fenrir::UIRoot& root = menu.Root();

    // Settings, clicked with the mouse once the pause panel has come in, opens the settings panel.
    demo.Frames(IntroFrames);
    const Fenrir::Math::Vec2 settings = DemoApp::Centre(root, root.FindById("settings"));
    demo.Feed().MouseMove(UIDemoTest::Window, settings, {0.0f, 0.0f});
    demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Feed().MouseButtonUp(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Frames(2);
    const Fenrir::ElementId entry = root.FindById("player-name");
    INFO("Settings did not open the settings panel's name entry");
    REQUIRE(entry.IsValid());

    REQUIRE(root.Focus(entry));
    auto* text = dynamic_cast<Fenrir::TextEntry*>(root.GetElement(entry));
    REQUIRE(text != nullptr);
    text->SelectAll();
    demo.Feed().Text(UIDemoTest::Window, U"Skadi");
    demo.Frames(2);

    Fenrir::UIRoot& hud = demo.HudRoot();
    INFO("the HUD's name reads \"" << std::string(hud.GetText(hud.FindById("player-name"))) << "\"");
    REQUIRE(hud.GetText(hud.FindById("player-name")) == "Skadi");
}
