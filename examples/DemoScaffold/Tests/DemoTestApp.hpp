#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUI/UITarget.hpp"

namespace DemoScaffoldTests
{
    /** @brief What a RecordingLogger was told, by level. */
    struct LoggedMessages
    {
        std::vector<std::string> infos;
        std::vector<std::string> errors;

        /** @brief Whether an info line contains `text`. */
        [[nodiscard]] bool InfoContains(std::string_view text) const
        {
            return std::any_of(infos.begin(), infos.end(),
                               [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
    };

    /**
     * @brief A logger that keeps its info and error lines and passes every line to its added loggers, as ConsoleLogger
     * does (a second ConsoleLogger in one process throws).
     */
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<LoggedMessages> messages) : m_messages(std::move(messages))
        {
        }

        void AddLogger(std::shared_ptr<ILogger> logger) override
        {
            m_sinks.push_back(std::move(logger));
        }
        void RemoveLogger(std::shared_ptr<ILogger> logger) override
        {
            m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), logger), m_sinks.end());
        }

      protected:
        void LogImpl(std::string_view message) override
        {
            for (const auto& sink : m_sinks)
            {
                sink->Log("{}", message);
            }
        }
        void InfoImpl(std::string_view message) override
        {
            m_messages->infos.emplace_back(message);
            for (const auto& sink : m_sinks)
            {
                sink->Info("{}", message);
            }
        }
        void WarnImpl(std::string_view message) override
        {
            for (const auto& sink : m_sinks)
            {
                sink->Warn("{}", message);
            }
        }
        void ErrorImpl(std::string_view message) override
        {
            m_messages->errors.emplace_back(message);
            for (const auto& sink : m_sinks)
            {
                sink->Error("{}", message);
            }
        }
        void FatalImpl(std::string_view message) override
        {
            m_messages->errors.emplace_back(message);
            for (const auto& sink : m_sinks)
            {
                sink->Fatal("{}", message);
            }
        }

      private:
        std::shared_ptr<LoggedMessages> m_messages;
        std::vector<std::shared_ptr<ILogger>> m_sinks;
    };

    /**
     * @brief The demo UI kit's 2D target in the tests: the null backend's 1280 by 720 surface at a content scale of
     * one, so the window's logical units are its own, keeping every list the kit's root submits.
     */
    class CaptureTarget : public Fenrir::UITarget
    {
      public:
        Fenrir::UITargetSize GetSize(Fenrir::App&) override
        {
            return {{1280.0f, 720.0f}, 1.0f};
        }
        void Submit(Fenrir::App&, Fenrir::DrawList2D list) override
        {
            lists.push_back(std::move(list));
        }
        std::vector<Fenrir::DrawList2D> lists;
    };

    /** @brief The texts of `root`'s elements of class `name`, in tree order. */
    inline std::vector<std::string> TextsOfClass(const Fenrir::UIRoot& root, std::string_view name)
    {
        std::vector<std::string> texts;
        for (const Fenrir::ElementId element : root.FindByClass(name))
        {
            texts.emplace_back(root.GetText(element));
        }
        return texts;
    }

    // The App's default fixed step: a frame of `oneFrame` seconds runs exactly one tick.
    inline constexpr double oneFrame = 1.0 / 60.0;

    /** @brief Options for a run on the null backend with no switch set. */
    inline FenrirDemo::DemoRunOptions NullRun()
    {
        FenrirDemo::DemoRunOptions run;
        run.backend = Fenrir::GraphicsBackend::Null;
        return run;
    }

    /** @brief A scene named `name` that spawns nothing. */
    inline FenrirDemo::DemoScene EmptyScene(std::string name)
    {
        FenrirDemo::DemoScene scene;
        scene.name = std::move(name);
        scene.spawn = [](Fenrir::App&, Fenrir::Scene&) {};
        return scene;
    }

    /** @brief An App with the demo scaffold installed on the null backend, driven one frame at a time. */
    struct DemoTestApp
    {
        std::shared_ptr<LoggedMessages> logged = std::make_shared<LoggedMessages>();
        std::shared_ptr<CaptureTarget> uiTarget = std::make_shared<CaptureTarget>();
        Fenrir::App app{std::make_unique<RecordingLogger>(logged), 2};
        FenrirDemo::DemoScaffold demo;

        explicit DemoTestApp(std::vector<FenrirDemo::DemoScene> scenes, FenrirDemo::DemoRunOptions run = NullRun(),
                             bool imguiOverlay = false, std::vector<FenrirDemo::DemoKeyRow> appKeyRows = {},
                             const std::function<void(FenrirDemo::DemoConfig&)>& configure = {})
            : demo(Install(app, std::move(scenes), std::move(run), imguiOverlay, uiTarget, std::move(appKeyRows),
                           configure))
        {
        }

        /** @brief The demo UI kit. */
        FenrirDemo::DemoUI& Kit()
        {
            return app.GetResource<FenrirDemo::DemoUI>();
        }

        /** @brief The texts of the kit's elements of class `name`, in tree order. */
        std::vector<std::string> Texts(std::string_view name)
        {
            return TextsOfClass(Kit().Root(), name);
        }

        /** @brief The centre of `element`'s border box in the window's logical units, where a press on it goes. */
        Fenrir::Math::Vec2 Centre(Fenrir::ElementId element)
        {
            const Fenrir::UIRoot& root = Kit().Root();
            const Fenrir::ElementBoxes boxes = root.GetBoxes(element);
            const Fenrir::Math::Vec2 centre{boxes.border.x + boxes.border.width * 0.5f,
                                            boxes.border.y + boxes.border.height * 0.5f};
            return centre * root.GetScale();
        }

        /** @brief Moves the pointer to `position` and presses and releases the left button there, across frames. */
        void Click(Fenrir::Math::Vec2 position)
        {
            Fenrir::InputFeed& feed = app.GetResource<Fenrir::InputFeed>();
            feed.MouseMove(Fenrir::WindowId{1}, position, {0.0f, 0.0f});
            Frames();
            feed.MouseButtonDown(Fenrir::WindowId{1}, Fenrir::MouseButton::Left);
            Frames();
            feed.MouseButtonUp(Fenrir::WindowId{1}, Fenrir::MouseButton::Left);
            Frames();
        }

        /** @brief Runs `count` frames of one tick each. */
        void Frames(int count = 1)
        {
            for (int i = 0; i < count; ++i)
            {
                app.RunFrame(oneFrame);
            }
        }

        /** @brief Presses and releases `key` across two frames, as a person tapping it would. */
        void Tap(Fenrir::Key key)
        {
            Fenrir::InputFeed& feed = app.GetResource<Fenrir::InputFeed>();
            feed.KeyDown(Fenrir::WindowId{1}, key, Scancode(key));
            Frames();
            feed.KeyUp(Fenrir::WindowId{1}, key, Scancode(key));
            Frames();
        }

        /** @brief The scancode named as `key` is: the scaffold's tests use a US layout. */
        static Fenrir::Scancode Scancode(Fenrir::Key key)
        {
            return static_cast<Fenrir::Scancode>(static_cast<std::uint16_t>(key));
        }

        /** @brief The active scene's name. */
        std::string ActiveScene()
        {
            return app.GetActiveScene().GetName();
        }

      private:
        static FenrirDemo::DemoScaffold Install(Fenrir::App& app, std::vector<FenrirDemo::DemoScene> scenes,
                                                FenrirDemo::DemoRunOptions run, bool imguiOverlay,
                                                std::shared_ptr<CaptureTarget> uiTarget,
                                                std::vector<FenrirDemo::DemoKeyRow> appKeyRows,
                                                const std::function<void(FenrirDemo::DemoConfig&)>& configure)
        {
            FenrirDemo::DemoConfig config;
            config.demoUITarget = std::move(uiTarget);
            config.appKeyRows = std::move(appKeyRows);
            config.appName = "TestDemo";
            config.scenes = std::move(scenes);
            config.run = std::move(run);
            config.imguiOverlay = imguiOverlay;
            // The rest of the config, for a test that needs a switch the arguments above do not name.
            if (configure)
            {
                configure(config);
            }
            return FenrirDemo::DemoScaffold::Install(app, std::move(config));
        }
    };
} // namespace DemoScaffoldTests
