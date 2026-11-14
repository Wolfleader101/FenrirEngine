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

    // The screen root's text entry's value, or "(none)" when it has no settings panel.
    std::string ScreenName(DemoApp& demo)
    {
        Fenrir::UIRoot& screen = demo.Menu().ScreenRoot();
        const auto* entry = dynamic_cast<const Fenrir::TextEntry*>(screen.GetElement(screen.FindById("player-name")));
        return entry != nullptr ? std::string(entry->GetValue()) : std::string("(none)");
    }
} // namespace

TEST_CASE("The room's screen holds the menu's document from the start, in an image of the screen's size")
{
    DemoApp demo;
    demo.Frames(2);
    UIDemo::Menu& menu = demo.Menu();
    Fenrir::UIRoot& screen = menu.ScreenRoot();

    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    REQUIRE(demo.errors->empty());
    INFO("the live menu is closed and the screen shows the pause panel anyway");
    REQUIRE_FALSE(menu.IsOpen());
    REQUIRE(screen.FindById("pause").IsValid());
    INFO("the screen's root is its image's size at a content scale of one, blending linear");
    REQUIRE(screen.GetTargetSize() ==
            Fenrir::UITargetSize{{static_cast<float>(UIDemo::ScreenWidth), static_cast<float>(UIDemo::ScreenHeight)},
                                 1.0f});
    REQUIRE(screen.GetConfig().blendSpace == Fenrir::BlendSpace2D::Linear);
    const Fenrir::ImageData* image = demo.app.GetResource<Fenrir::AssetStore>().TryGet(menu.ScreenImage());
    REQUIRE(image != nullptr);
    REQUIRE(image->width == UIDemo::ScreenWidth);
    REQUIRE(image->height == UIDemo::ScreenHeight);
    INFO("the 1080 UI units of the menu span the image's 720 pixels");
    REQUIRE(screen.GetScale() == static_cast<float>(UIDemo::ScreenHeight) / 1080.0f);
}

TEST_CASE("The screen mirrors the live menu: its settings panel, the kept settings and the focus")
{
    DemoApp demo;
    demo.Frames(2);
    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frames(IntroFrames);
    Fenrir::UIRoot& root = demo.Menu().Root();
    Fenrir::UIRoot& screen = demo.Menu().ScreenRoot();

    // Settings, clicked with the mouse, opens the settings panel in the live menu.
    const Fenrir::Math::Vec2 settings = DemoApp::Centre(root, root.FindById("settings"));
    demo.Feed().MouseMove(UIDemoTest::Window, settings, {0.0f, 0.0f});
    demo.Feed().MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Feed().MouseButtonUp(UIDemoTest::Window, Fenrir::MouseButton::Left);
    demo.Frames(2);
    INFO("the screen opened its settings panel with the live one's, showing the kept name");
    REQUIRE(root.FindById("settings-panel").IsValid());
    REQUIRE(screen.FindById("settings-panel").IsValid());
    REQUIRE(ScreenName(demo) == "Player");
    INFO("the screen's focus is on its Settings button, as the live one is after the click");
    REQUIRE(root.GetIdString(root.GetFocused()) == "settings");
    REQUIRE(screen.GetIdString(screen.GetFocused()) == "settings");

    const Fenrir::ElementId entry = root.FindById("player-name");
    auto* text = dynamic_cast<Fenrir::TextEntry*>(root.GetElement(entry));
    REQUIRE(text != nullptr);
    REQUIRE(root.Focus(entry));
    text->SelectAll();
    demo.Feed().Text(UIDemoTest::Window, U"Skadi");
    demo.Frames(2);
    INFO("the name typed into the live menu shows on the screen, and the screen's focus followed to its entry");
    REQUIRE(ScreenName(demo) == "Skadi");
    REQUIRE(screen.GetIdString(screen.GetFocused()) == "player-name");

    // Escape takes the entry's focus, then closes the settings panel.
    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frame();
    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frames(OutroFrames);
    INFO("the screen's settings panel left with the live one's");
    REQUIRE_FALSE(root.FindById("settings-panel").IsValid());
    REQUIRE_FALSE(screen.FindById("settings-panel").IsValid());
    INFO("the screen still shows the pause panel");
    REQUIRE(screen.FindById("pause").IsValid());
    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    REQUIRE(demo.errors->empty());
}
