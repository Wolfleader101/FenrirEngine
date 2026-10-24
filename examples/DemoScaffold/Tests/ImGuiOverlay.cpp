// The ImGuiOverlay as an input router (ADR-0027): what ImGui takes never reaches the scene, and what it claims is
// published. A demo app on the null backend with the overlay on, its input fed through the input feed.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "DemoTestApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"

using namespace DemoScaffoldTests;

namespace
{
    constexpr Fenrir::WindowId mainWindow{1};

    // What the panel scene's ImGui window holds, and where its text field was laid out.
    struct Panel
    {
        std::array<char, 64> text{};
        Fenrir::Math::Vec2 fieldCentre{0.0f, 0.0f};
    };

    // A scene with one fixed ImGui window, at (100, 100) and 300 x 120, holding a text field.
    FenrirDemo::DemoScene PanelScene(std::shared_ptr<Panel> panel)
    {
        FenrirDemo::DemoScene scene = EmptyScene("Panel");
        scene.update = [panel](Fenrir::App&, Fenrir::Scene&) {
            ImGui::SetNextWindowPos({100.0f, 100.0f});
            ImGui::SetNextWindowSize({300.0f, 120.0f});
            ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            ImGui::InputText("Name", panel->text.data(), panel->text.size());
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            panel->fieldCentre = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
            ImGui::End();
        };
        return scene;
    }

    // Over the panel scene's window, but not its text field; and well away from the window.
    const Fenrir::Math::Vec2 overWindow{250.0f, 200.0f};
    const Fenrir::Math::Vec2 awayFromWindow{800.0f, 500.0f};

    // The panel scene with the overlay on, keeping every event the game was sent.
    struct OverlayTest
    {
        std::shared_ptr<Panel> panel = std::make_shared<Panel>();
        DemoTestApp test{{PanelScene(panel)}, NullRun(), true};

        Fenrir::EventReader<FenrirDemo::DemoSceneClickEvent> clickReader;
        Fenrir::EventReader<Fenrir::KeyEvent> keyReader;
        Fenrir::EventReader<Fenrir::TextEvent> textReader;
        Fenrir::EventReader<Fenrir::MouseButtonEvent> buttonReader;
        std::size_t sceneClicks = 0;
        std::vector<Fenrir::KeyEvent> keys;
        std::size_t texts = 0;
        std::size_t buttons = 0;

        OverlayTest()
        {
            // The window is laid out, and the field's place known, after the first frames.
            Frames(2);
            Forget();
        }

        Fenrir::InputFeed& Feed()
        {
            return test.app.GetResource<Fenrir::InputFeed>();
        }

        // Runs `count` frames, keeping what the game was sent in each.
        void Frames(int count = 1)
        {
            for (int i = 0; i < count; ++i)
            {
                test.Frames();
                sceneClicks += clickReader.Read(test.app).size();
                for (const Fenrir::KeyEvent& event : keyReader.Read(test.app))
                {
                    keys.push_back(event);
                }
                texts += textReader.Read(test.app).size();
                buttons += buttonReader.Read(test.app).size();
            }
        }

        // Forgets the events kept so far.
        void Forget()
        {
            sceneClicks = 0;
            keys.clear();
            texts = 0;
            buttons = 0;
        }

        // Moves the pointer to `position` and lets ImGui see it there.
        void PointAt(Fenrir::Math::Vec2 position)
        {
            Feed().MouseMove(mainWindow, position, {0.0f, 0.0f});
            Frames(2);
        }

        // Clicks the left button where the pointer is, across frames as a person would.
        void Click()
        {
            Feed().MouseButtonDown(mainWindow, Fenrir::MouseButton::Left);
            Frames();
            Feed().MouseButtonUp(mainWindow, Fenrir::MouseButton::Left);
            Frames(2);
        }

        Fenrir::WindowInputClaims Claims()
        {
            return test.app.GetResource<Fenrir::InputClaims>().For(mainWindow);
        }

        bool SceneHolds(Fenrir::Key key)
        {
            return test.app.GetResource<Fenrir::Keyboard>().IsDown(key);
        }
    };
} // namespace

TEST_CASE("A click on an ImGui window is consumed and never reaches the scene-click event", "[demo][imgui]")
{
    OverlayTest overlay;
    overlay.PointAt(overWindow);
    INFO("the pointer over the ImGui window raises the pointer claim");
    REQUIRE(overlay.Claims().pointer);

    overlay.Click();
    INFO("the click on the window sent no scene click");
    REQUIRE(overlay.sceneClicks == 0);
    INFO("nor did the game see the button go down or up");
    REQUIRE(overlay.buttons == 0);

    overlay.PointAt(awayFromWindow);
    INFO("away from the window the pointer claim is down");
    REQUIRE_FALSE(overlay.Claims().pointer);
    overlay.Click();
    INFO("a click away from the window is the scene's");
    REQUIRE(overlay.sceneClicks == 1);
    REQUIRE(overlay.buttons == 2);
}

TEST_CASE("Typing into a focused ImGui text field claims text input, and the scene's keys see nothing", "[demo][imgui]")
{
    OverlayTest overlay;
    overlay.PointAt(overlay.panel->fieldCentre);
    overlay.Click();

    const Fenrir::WindowInputClaims claims = overlay.Claims();
    INFO("the focused field claims the keyboard and text input");
    REQUIRE(claims.keyboardFocus);
    REQUIRE(claims.textInput);
    INFO("with its caret, a line tall, inside the panel");
    REQUIRE(claims.caret.position.x >= 100.0f);
    REQUIRE(claims.caret.position.x <= 400.0f);
    REQUIRE(claims.caret.position.y >= 100.0f);
    REQUIRE(claims.caret.position.y <= 220.0f);
    REQUIRE(claims.caret.size.y > 0.0f);

    overlay.Forget();
    overlay.Feed().KeyDown(mainWindow, Fenrir::Key::H, Fenrir::Scancode::H);
    overlay.Feed().Text(mainWindow, U"hi");
    overlay.Frames();
    INFO("the scene does not see the key held");
    REQUIRE_FALSE(overlay.SceneHolds(Fenrir::Key::H));
    overlay.Feed().KeyUp(mainWindow, Fenrir::Key::H, Fenrir::Scancode::H);
    overlay.Frames(2);
    INFO("nor any key or text event");
    REQUIRE(overlay.keys.empty());
    REQUIRE(overlay.texts == 0);
    INFO("the text went into the field");
    REQUIRE(std::string(overlay.panel->text.data()) == "hi");
}

TEST_CASE("Focusing an ImGui text field while a scene key is held cancels the key", "[demo][imgui]")
{
    OverlayTest overlay;
    overlay.Feed().KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
    overlay.Frames();
    INFO("the scene has the key held");
    REQUIRE(overlay.SceneHolds(Fenrir::Key::W));
    overlay.Forget();

    overlay.PointAt(overlay.panel->fieldCentre);
    overlay.Click();

    INFO("focusing the field let go of the scene's key");
    REQUIRE_FALSE(overlay.SceneHolds(Fenrir::Key::W));
    INFO("with one release, marked cancelled");
    REQUIRE(overlay.keys.size() == 1);
    REQUIRE(overlay.keys[0].key == Fenrir::Key::W);
    REQUIRE_FALSE(overlay.keys[0].pressed);
    REQUIRE(overlay.keys[0].cancelled);
}

TEST_CASE("Clicking the scene away from ImGui gives the keyboard back to the scene", "[demo][imgui]")
{
    OverlayTest overlay;
    overlay.PointAt(overlay.panel->fieldCentre);
    overlay.Click();
    INFO("the field has the keyboard");
    REQUIRE(overlay.Claims().keyboardFocus);

    overlay.PointAt(awayFromWindow);
    overlay.Click();
    INFO("clicking the scene drops ImGui's keyboard and text claims");
    REQUIRE_FALSE(overlay.Claims().keyboardFocus);
    REQUIRE_FALSE(overlay.Claims().textInput);

    overlay.Feed().KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
    overlay.Frames();
    INFO("the scene's keys work again");
    REQUIRE(overlay.SceneHolds(Fenrir::Key::W));
}

TEST_CASE("Dragging in ImGui with the right button does not turn the free-fly camera", "[demo][imgui]")
{
    OverlayTest overlay;
    overlay.PointAt(overWindow);
    const Fenrir::Math::Quat before = overlay.test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot;

    overlay.Feed().MouseButtonDown(mainWindow, Fenrir::MouseButton::Right);
    overlay.Frames();
    overlay.Feed().MouseMove(mainWindow, {350.0f, 250.0f}, {100.0f, 50.0f});
    overlay.Frames();
    INFO("the drag that began on ImGui captures the pointer");
    REQUIRE(overlay.Claims().pointerCapture);
    overlay.Feed().MouseButtonUp(mainWindow, Fenrir::MouseButton::Right);
    overlay.Frames(2);

    const Fenrir::Math::Quat after = overlay.test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot;
    INFO("the camera looks where it did");
    REQUIRE(after == before);
    INFO("the game saw neither the button nor the drag");
    REQUIRE(overlay.buttons == 0);
    INFO("and the capture ended with the drag");
    REQUIRE_FALSE(overlay.Claims().pointerCapture);
}
