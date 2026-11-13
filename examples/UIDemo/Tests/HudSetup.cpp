#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UITarget.hpp"
#include "UIDemo.hpp"

// UIDemo's HUD set up as the app sets it up, on the null backend with no window, the App driven a frame at a time. The
// HUD root draws into a capturing target of 1920 by 1080, so 1080 UI units span its height at a scale of one.
namespace
{
    // A logger that keeps its error lines: a second ConsoleLogger in one process throws.
    class ErrorLogger : public Fenrir::ILogger
    {
      public:
        explicit ErrorLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
        {
        }
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    class CaptureTarget : public Fenrir::UITarget
    {
      public:
        Fenrir::UITargetSize GetSize(Fenrir::App&) override
        {
            return {{1920.0f, 1080.0f}, 1.0f};
        }
        void Submit(Fenrir::App&, Fenrir::DrawList2D list) override
        {
            lists.push_back(std::move(list));
        }
        std::vector<Fenrir::DrawList2D> lists;
    };

    struct HudApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        std::shared_ptr<CaptureTarget> target = std::make_shared<CaptureTarget>();
        Fenrir::App app{std::make_unique<ErrorLogger>(errors), 2};
        FenrirDemo::DemoScaffold demo{Install(app, target)};

        static FenrirDemo::DemoScaffold Install(Fenrir::App& app, std::shared_ptr<CaptureTarget> target)
        {
            UIDemo::HudConfig hud;
            hud.target = std::move(target);
            FenrirDemo::DemoConfig config = UIDemo::MakeDemoConfig(std::move(hud));
            FenrirDemo::DemoRunOptions run;
            run.backend = Fenrir::GraphicsBackend::Null;
            config.run = run;
            return FenrirDemo::DemoScaffold::Install(app, std::move(config));
        }

        void Frame()
        {
            app.RunFrame(1.0 / 60.0);
        }

        Fenrir::UIRoot& Root()
        {
            return *app.GetResource<Fenrir::UIRoots>().GetRoots().front();
        }

        Fenrir::Entity Player()
        {
            Fenrir::EntityList& entities = app.GetActiveScene().GetEntityList();
            const std::array<Fenrir::ComponentType, 1> health{Fenrir::ComponentType::Native<UIDemo::Health>()};
            Fenrir::Entity found;
            entities.ForEachWith(health, [&](std::uint32_t id) { found = entities.GetEntity(id); });
            return found;
        }

        // The share of a progress bar's content box its fill covers.
        float FillShare(std::string_view bar)
        {
            Fenrir::UIRoot& root = Root();
            const Fenrir::ElementId element = root.FindById(bar);
            const Fenrir::ElementId fill = root.GetChildren(element).front();
            return root.GetBoxes(fill).border.width / root.GetBoxes(element).content.width;
        }
    };

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

    Fenrir::UIRoot& root = hud.Root();
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

        Fenrir::UIRoot& root = hud.Root();
        const Fenrir::ElementBoxes boxes = root.GetBoxes(root.FindById(id));
        const Fenrir::Math::Vec2 centre{boxes.border.x + boxes.border.width * 0.5f,
                                        boxes.border.y + boxes.border.height * 0.5f};
        INFO("a point over #" << id << " hits an element of the HUD");
        REQUIRE_FALSE(root.HitTest(centre).IsValid());

        // The target is 1920 by 1080 logical units at a scale of one, so UI units are the window's.
        auto& feed = hud.app.GetResource<Fenrir::InputFeed>();
        feed.MouseMove(Fenrir::WindowId{1}, centre, {0.0f, 0.0f});
        feed.MouseButtonDown(Fenrir::WindowId{1}, Fenrir::MouseButton::Right);
        hud.Frame();
        INFO("a right press over #" << id << " did not reach the game's Mouse");
        REQUIRE(hud.app.GetResource<Fenrir::Mouse>().IsDown(Fenrir::MouseButton::Right));
    }
}
