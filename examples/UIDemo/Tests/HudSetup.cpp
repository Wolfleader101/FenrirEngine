#include <cmath>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "DemoApp.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/UIRoot.hpp"

// UIDemo's HUD set up as the app sets it up (DemoApp.hpp), driven a frame at a time.
namespace
{
    using HudApp = UIDemoTest::DemoApp;

    // Within a pixel of a 400-unit bar, so the snapped edges pass.
    constexpr float Tolerance = 1.0f / 400.0f;
} // namespace

TEST_CASE("The HUD's health bar follows the player's health")
{
    HudApp hud;
    // The first frame enters the room, which spawns the player.
    hud.Frame();
    INFO("the HUD loaded with errors: " << (hud.errors->empty() ? "" : hud.errors->front()));
    REQUIRE(hud.errors->empty());
    Fenrir::Entity player = hud.Player();
    INFO("the room spawned no player");
    REQUIRE(player.IsValid());

    for (const float health : {25.0f, 70.0f})
    {
        player.GetComponent<UIDemo::Health>() = {health, 100.0f};
        hud.Frame();
        INFO("health " << health << " of 100, the bar's fill covers " << hud.FillShare("health"));
        REQUIRE(std::abs(hud.FillShare("health") - health / 100.0f) <= Tolerance);
    }
}

TEST_CASE("The HUD's stamina bar follows the player's stamina")
{
    HudApp hud;
    hud.Frame();
    Fenrir::Entity player = hud.Player();
    INFO("the room spawned no player");
    REQUIRE(player.IsValid());

    for (const float stamina : {10.0f, 35.0f})
    {
        player.GetComponent<UIDemo::Stamina>() = {stamina, 50.0f};
        hud.Frame();
        // The camera stands still, so the room refills stamina at 20 a second over the frame before the HUD shows it.
        const float shown = (stamina + 20.0f / 60.0f) / 50.0f;
        INFO("stamina " << stamina << " of 50 before the frame, the bar's fill covers " << hud.FillShare("stamina"));
        REQUIRE(std::abs(hud.FillShare("stamina") - shown) <= Tolerance);
    }
}

TEST_CASE("The HUD's ammo counter shows the player's ammo")
{
    HudApp hud;
    hud.Frame();
    Fenrir::Entity player = hud.Player();
    INFO("the room spawned no player");
    REQUIRE(player.IsValid());
    player.GetComponent<UIDemo::Ammo>() = {7, 30, 42};
    hud.Frame();

    Fenrir::UIRoot& root = hud.HudRoot();
    INFO("the ammo counter reads \"" << root.GetText(root.FindById("ammo")) << "\"");
    REQUIRE(root.GetText(root.FindById("ammo")) == "7 / 42");
}

TEST_CASE("A click over the HUD reaches the game, so the camera still turns under the mouse")
{
    for (const std::string_view id : {"health", "ammo", "compass", "minimap"})
    {
        HudApp hud;
        hud.Frame();
        hud.Frame();

        Fenrir::UIRoot& root = hud.HudRoot();
        const Fenrir::ElementBoxes boxes = root.GetBoxes(root.FindById(id));
        const Fenrir::Math::Vec2 centre{boxes.border.x + boxes.border.width * 0.5f,
                                        boxes.border.y + boxes.border.height * 0.5f};
        INFO("a point over #" << id << " hits an element of the HUD");
        REQUIRE_FALSE(root.HitTest(centre).IsValid());

        // The target is 1920 by 1080 logical units at a scale of one, so UI units are the window's.
        auto& feed = hud.app.GetResource<Fenrir::InputFeed>();
        feed.MouseMove(UIDemoTest::Window, centre, {0.0f, 0.0f});
        feed.MouseButtonDown(UIDemoTest::Window, Fenrir::MouseButton::Right);
        hud.Frame();
        INFO("a right press over #" << id << " did not reach the game's Mouse");
        REQUIRE(hud.app.GetResource<Fenrir::Mouse>().IsDown(Fenrir::MouseButton::Right));
    }
}
