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

    void TapEscape(DemoApp& demo)
    {
        demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    }

    // Enters the room, presses Escape and runs the frames that lay the menu out.
    void OpenMenu(DemoApp& demo)
    {
        demo.Frames(2);
        TapEscape(demo);
        demo.Frames(2);
    }

    // Presses the right mouse button away from the middle, and whether the game's Mouse then holds it.
    bool RightPressReachesGame(DemoApp& demo)
    {
        demo.Feed().MouseMove(UIDemoTest::Window, {200.0f, 200.0f}, {0.0f, 0.0f});
        demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Right);
        demo.Frame();
        return demo.app.GetResource<Fenrir::Mouse>().IsDown(Fenrir::MouseButton::Right);
    }

    // A direction gives Resume the focus, and A clicks it.
    void ResumeByGamepad(DemoApp& demo)
    {
        Fenrir::UIRoot& root = demo.Menu().Root();
        TapButton(demo, Fenrir::GamepadButton::DPadDown);
        demo.Frame();
        INFO("the first direction did not give Resume the focus");
        REQUIRE(root.GetIdString(root.GetFocused()) == "resume");
        TapButton(demo, Fenrir::GamepadButton::South);
        demo.Frame();
    }
} // namespace

TEST_CASE("Escape opens the pause menu, whose root then takes pointer input from the game")
{
    DemoApp demo;
    demo.Frames(2);
    UIDemo::Menu& menu = demo.Menu();
    INFO("the menu starts open");
    REQUIRE_FALSE(menu.IsOpen());
    INFO("the closed menu has an element under the middle of the window");
    REQUIRE_FALSE(menu.Root().HitTest(Middle).IsValid());

    TapEscape(demo);
    demo.Frames(2);
    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    REQUIRE(demo.errors->empty());
    INFO("Escape did not open the menu");
    REQUIRE(menu.IsOpen());
    INFO("the open menu has no element under the middle of the window");
    REQUIRE(menu.Root().HitTest(Middle).IsValid());
    // The free-fly camera turns while the game's Mouse holds the right button; the menu keeps it.
    INFO("a right press with the menu open reached the game's Mouse");
    REQUIRE_FALSE(RightPressReachesGame(demo));
}

TEST_CASE("A gamepad direction and A click Resume, which closes the menu")
{
    DemoApp demo;
    demo.Feed().GamepadConnected(Pad);
    OpenMenu(demo);
    INFO("Escape did not open the menu");
    REQUIRE(demo.Menu().IsOpen());

    ResumeByGamepad(demo);
    INFO("Resume, clicked with A, did not close the menu");
    REQUIRE_FALSE(demo.Menu().IsOpen());
}

TEST_CASE("Once the closed menu's outro ends, the game has the pointer again")
{
    DemoApp demo;
    demo.Feed().GamepadConnected(Pad);
    OpenMenu(demo);
    ResumeByGamepad(demo);
    demo.Frames(OutroFrames);
    Fenrir::UIRoot& root = demo.Menu().Root();
    INFO("the menu's elements are still there once its outro has ended");
    REQUIRE_FALSE(root.HitTest(Middle).IsValid());
    INFO("a right press with the menu closed did not reach the game's Mouse");
    REQUIRE(RightPressReachesGame(demo));
}

TEST_CASE("A menu opened again during the last one's outro closes again, giving the game the pointer back")
{
    DemoApp demo;
    OpenMenu(demo);
    TapEscape(demo);
    demo.Frame();
    // The first menu is in its outro, still there with the same ids.
    TapEscape(demo);
    demo.Frames(2);
    INFO("Escape during the outro did not open the menu again");
    REQUIRE(demo.Menu().IsOpen());
    TapEscape(demo);
    demo.Frame();
    INFO("Escape did not close the second menu");
    REQUIRE_FALSE(demo.Menu().IsOpen());

    demo.Frames(OutroFrames);
    INFO("a menu is still there once both outros have ended");
    REQUIRE_FALSE(demo.Menu().Root().HitTest(Middle).IsValid());
}

TEST_CASE("Tab with nothing focused gives Resume the focus")
{
    DemoApp demo;
    OpenMenu(demo);
    Fenrir::UIRoot& root = demo.Menu().Root();
    INFO("something has the focus before Tab");
    REQUIRE_FALSE(root.GetFocused().IsValid());
    demo.Tap(Fenrir::Key::Tab, Fenrir::Scancode::Tab);
    demo.Frame();
    INFO("Tab gave the focus to #" << std::string(root.GetIdString(root.GetFocused())));
    REQUIRE(root.GetIdString(root.GetFocused()) == "resume");
}

TEST_CASE("The name typed into the settings panel shows on the HUD")
{
    DemoApp demo;
    OpenMenu(demo);
    Fenrir::UIRoot& root = demo.Menu().Root();

    // Settings, clicked with the mouse once the pause panel has come in, opens the settings panel.
    demo.Frames(IntroFrames);
    const Fenrir::Math::Vec2 settings = DemoApp::Centre(root, root.FindById("settings"));
    demo.Feed().MouseMove(UIDemoTest::Window, settings, {0.0f, 0.0f});
    demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Feed().MouseButtonUp(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Frames(2);
    const Fenrir::ElementId entry = root.FindById("player-name");
    auto* text = dynamic_cast<Fenrir::TextEntry*>(root.GetElement(entry));
    INFO("Settings did not open the settings panel's name entry");
    REQUIRE(text != nullptr);
    INFO("the name entry did not take the focus");
    REQUIRE(root.Focus(entry));

    text->SelectAll();
    demo.Feed().Text(UIDemoTest::Window, U"Skadi");
    demo.Frames(2);
    Fenrir::UIRoot& hud = demo.HudRoot();
    INFO("the HUD's name reads \"" << std::string(hud.GetText(hud.FindById("player-name"))) << "\"");
    REQUIRE(hud.GetText(hud.FindById("player-name")) == "Skadi");
}
