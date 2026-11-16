#include <string>

#include <catch2/catch_test_macros.hpp>

#include "DemoApp.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIRoot.hpp"

// The room's screen (DemoApp.hpp): the pause menu's document in a root drawn into an image, mirroring the live menu.
namespace
{
    using UIDemoTest::DemoApp;

    // More than the panels' intro and outro last.
    constexpr int IntroFrames = 30;
    constexpr int OutroFrames = 40;

    // The value of the screen root's player-name entry, or "(none)" when it has no settings panel.
    std::string ScreenPlayerName(DemoApp& demo)
    {
        Fenrir::UIRoot& screenRoot = demo.Menu().ScreenRoot();
        const auto* entry =
            dynamic_cast<const Fenrir::TextEntry*>(screenRoot.GetElement(screenRoot.FindById("player-name")));
        return entry != nullptr ? std::string(entry->GetValue()) : std::string("(none)");
    }

    // Enters the room, opens the menu, and clicks Settings with the mouse once the pause panel has come in.
    void OpenSettings(DemoApp& demo)
    {
        demo.Frames(2);
        demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
        demo.Frames(IntroFrames);
        Fenrir::UIRoot& root = demo.Menu().Root();
        const Fenrir::Math::Vec2 settings = DemoApp::Centre(root, root.FindById("settings"));
        demo.Feed().MouseMove(UIDemoTest::Window, settings, {0.0f, 0.0f});
        demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Left);
        demo.Feed().MouseButtonUp(UIDemoTest::Window, Fenrir::MouseButton::Left);
        demo.Frames(2);
        INFO("Settings did not open the live settings panel");
        REQUIRE(root.FindById("settings-panel").IsValid());
    }
} // namespace

TEST_CASE("The screen's root holds the pause panel from the start, with the live menu closed")
{
    DemoApp demo;
    demo.Frames(2);

    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    REQUIRE(demo.errors->empty());
    REQUIRE_FALSE(demo.Menu().IsOpen());
    INFO("the screen's root has no #pause");
    REQUIRE(demo.Menu().ScreenRoot().FindById("pause").IsValid());
}

TEST_CASE("The screen's root draws into an image of 1280 by 720, at the 1080 scale rule's 720 / 1080")
{
    DemoApp demo;
    demo.Frames(2);
    UIDemo::Menu& menu = demo.Menu();
    Fenrir::UIRoot& screenRoot = menu.ScreenRoot();

    const Fenrir::ImageData* image = demo.app.GetResource<Fenrir::AssetStore>().TryGet(menu.ScreenImage());
    INFO("the screen's image is missing, or not 1280 by 720");
    REQUIRE(image != nullptr);
    REQUIRE(image->width == 1280);
    REQUIRE(image->height == 720);
    INFO("the screen's root is not the image's size at a content scale of one");
    REQUIRE(screenRoot.GetTargetSize() == Fenrir::UITargetSize{{1280.0f, 720.0f}, 1.0f});
    INFO("the root's scale is " << screenRoot.GetScale() << ", not 720 / 1080");
    REQUIRE(screenRoot.GetScale() == 720.0f / 1080.0f);
    INFO("the screen's root does not blend linear");
    REQUIRE(screenRoot.GetConfig().blendSpace == Fenrir::BlendSpace2D::Linear);
}

TEST_CASE("The screen's settings panel opens and closes with the live one's")
{
    DemoApp demo;
    OpenSettings(demo);
    Fenrir::UIRoot& screenRoot = demo.Menu().ScreenRoot();
    INFO("the screen's root did not open its settings panel with the live one's");
    REQUIRE(screenRoot.FindById("settings-panel").IsValid());

    // Back closes the live settings panel.
    Fenrir::UIRoot& root = demo.Menu().Root();
    const Fenrir::Math::Vec2 back = DemoApp::Centre(root, root.FindById("back"));
    demo.Feed().MouseMove(UIDemoTest::Window, back, {0.0f, 0.0f});
    demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Feed().MouseButtonUp(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Frames(OutroFrames);
    INFO("the live settings panel did not close on Back");
    REQUIRE_FALSE(root.FindById("settings-panel").IsValid());
    INFO("the screen's settings panel stayed after the live one left");
    REQUIRE_FALSE(screenRoot.FindById("settings-panel").IsValid());
    INFO("the screen's pause panel did not stay");
    REQUIRE(screenRoot.FindById("pause").IsValid());
    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    REQUIRE(demo.errors->empty());
}

TEST_CASE("The screen's controls show the kept settings as the live menu changes them")
{
    DemoApp demo;
    OpenSettings(demo);
    INFO("the screen's name entry reads \"" << ScreenPlayerName(demo) << "\", not the kept \"Player\"");
    REQUIRE(ScreenPlayerName(demo) == "Player");

    Fenrir::UIRoot& root = demo.Menu().Root();
    const Fenrir::ElementId entry = root.FindById("player-name");
    auto* text = dynamic_cast<Fenrir::TextEntry*>(root.GetElement(entry));
    INFO("the live settings panel has no name entry that takes the focus");
    REQUIRE(text != nullptr);
    REQUIRE(root.Focus(entry));
    text->SelectAll();
    demo.Feed().Text(UIDemoTest::Window, U"Skadi");
    demo.Frames(2);
    INFO("the screen's name entry reads \"" << ScreenPlayerName(demo) << "\", not the typed \"Skadi\"");
    REQUIRE(ScreenPlayerName(demo) == "Skadi");
}

TEST_CASE("The screen's focus is on the element with the live focus's id")
{
    DemoApp demo;
    OpenSettings(demo);
    Fenrir::UIRoot& root = demo.Menu().Root();
    Fenrir::UIRoot& screenRoot = demo.Menu().ScreenRoot();
    INFO("after the click, the screen's focus is on #" << std::string(screenRoot.GetIdString(screenRoot.GetFocused()))
                                                       << ", not #settings");
    REQUIRE(screenRoot.GetIdString(screenRoot.GetFocused()) == "settings");

    INFO("the live name entry did not take the focus");
    REQUIRE(root.Focus(root.FindById("player-name")));
    demo.Frame();
    INFO("the screen's focus is on #" << std::string(screenRoot.GetIdString(screenRoot.GetFocused()))
                                      << ", not #player-name");
    REQUIRE(screenRoot.GetIdString(screenRoot.GetFocused()) == "player-name");
}
