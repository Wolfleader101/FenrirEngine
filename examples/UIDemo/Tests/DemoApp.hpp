#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UITarget.hpp"
#include "UIDemo.hpp"

// UIDemo set up as the app sets it up, on the null backend with no window, the App driven a frame at a time. The HUD's
// and the menu's roots draw into capturing targets of 1920 by 1080, so 1080 UI units span their height at a scale of
// one, and UI units are the window's logical units.
namespace UIDemoTest
{
    /** @brief A logger that keeps its error lines: a second ConsoleLogger in one process throws. */
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

    /** @brief A 1920 by 1080 2D target at a content scale of one that keeps every list a root submits to it. */
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

    /** @brief The window FenrirInput's feed sends to: the roots on custom targets still route its input. */
    inline constexpr Fenrir::WindowId Window{1};

    /** @brief UIDemo installed as main installs it, its HUD and menu roots drawing into capture targets. */
    struct DemoApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        std::shared_ptr<CaptureTarget> hudTarget = std::make_shared<CaptureTarget>();
        std::shared_ptr<CaptureTarget> menuTarget = std::make_shared<CaptureTarget>();
        Fenrir::App app{std::make_unique<ErrorLogger>(errors), 2};
        FenrirDemo::DemoScaffold demo{Install(app, hudTarget, menuTarget)};

        static FenrirDemo::DemoScaffold Install(Fenrir::App& app, std::shared_ptr<CaptureTarget> hudTarget,
                                                std::shared_ptr<CaptureTarget> menuTarget)
        {
            UIDemo::HudConfig hud;
            hud.target = std::move(hudTarget);
            UIDemo::MenuConfig menu;
            menu.target = std::move(menuTarget);
            FenrirDemo::DemoConfig config = UIDemo::MakeDemoConfig(std::move(hud), std::move(menu));
            FenrirDemo::DemoRunOptions run;
            run.backend = Fenrir::GraphicsBackend::Null;
            config.run = run;
            return FenrirDemo::DemoScaffold::Install(app, std::move(config));
        }

        /** @brief Runs one frame of 1/60 s. */
        void Frame()
        {
            app.RunFrame(1.0 / 60.0);
        }

        /** @brief Runs `count` frames of 1/60 s. */
        void Frames(int count)
        {
            for (int i = 0; i < count; ++i)
            {
                Frame();
            }
        }

        /** @brief FenrirInput's test feed. */
        Fenrir::InputFeed& Feed()
        {
            return app.GetResource<Fenrir::InputFeed>();
        }

        /** @brief Presses and releases `key`, fed for the next frame. */
        void Tap(Fenrir::Key key, Fenrir::Scancode scancode)
        {
            Feed().KeyDown(Window, key, scancode);
            Feed().KeyUp(Window, key, scancode);
        }

        /** @brief The HUD's root. */
        Fenrir::UIRoot& HudRoot()
        {
            return app.GetResource<UIDemo::Hud>().Root();
        }

        /** @brief The pause menu. */
        UIDemo::Menu& Menu()
        {
            return app.GetResource<UIDemo::Menu>();
        }

        /** @brief The room's player, the entity with Health; null before the room is entered. */
        Fenrir::Entity Player()
        {
            Fenrir::EntityList& entities = app.GetActiveScene().GetEntityList();
            const std::array<Fenrir::ComponentType, 1> health{Fenrir::ComponentType::Native<UIDemo::Health>()};
            Fenrir::Entity found;
            entities.ForEachWith(health, [&](std::uint32_t id) { found = entities.GetEntity(id); });
            return found;
        }

        /** @brief The share of the HUD's progress bar `bar`'s content box its fill covers. */
        float FillShare(std::string_view bar)
        {
            Fenrir::UIRoot& root = HudRoot();
            const Fenrir::ElementId element = root.FindById(bar);
            const Fenrir::ElementId fill = root.GetChildren(element).front();
            return root.GetBoxes(fill).border.width / root.GetBoxes(element).content.width;
        }

        /** @brief The centre of `element`'s border box, in UI units. */
        static Fenrir::Math::Vec2 Centre(const Fenrir::UIRoot& root, Fenrir::ElementId element)
        {
            const Fenrir::ElementBoxes boxes = root.GetBoxes(element);
            return {boxes.border.x + boxes.border.width * 0.5f, boxes.border.y + boxes.border.height * 0.5f};
        }
    };
} // namespace UIDemoTest
