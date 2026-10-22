#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirWindow/Window.hpp"

namespace WindowTests
{
    // A logger that keeps the errors and warnings it is given, for tests that check a diagnostic; the rest it drops (a
    // second ConsoleLogger in one process throws).
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
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
        void WarnImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
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

    // Every event `reader` has not read yet, copied out.
    template <typename TEvent>
    std::vector<TEvent> ReadAll(Fenrir::EventReader<TEvent>& reader, const Fenrir::App& app)
    {
        std::vector<TEvent> events;
        for (const TEvent& event : reader.Read(app))
        {
            events.push_back(event);
        }
        return events;
    }

    // The App's default fixed step: a frame of this many seconds runs one tick.
    inline constexpr double oneTick = 1.0 / 60.0;

    // A hidden main window's config, as GL tests will use (spec: "FenrirWindow").
    inline Fenrir::WindowConfig HiddenWindow()
    {
        Fenrir::WindowConfig config;
        config.title = "FenrirWindowTests";
        config.logicalSize = {320, 240};
        config.visible = false;
        return config;
    }

    // An App with FenrirWindow installed, driven one frame at a time. The App is declared first so it outlives the
    // Window owner.
    struct WindowTestApp
    {
        explicit WindowTestApp(Fenrir::WindowConfig config = HiddenWindow())
            : window(Fenrir::Window::Install(app, std::move(config)))
        {
        }

        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors), 1};
        Fenrir::Window window;

        // Skips the calling test when SDL's video could not start: no display to make a window on.
        void RequireDisplay() const
        {
            if (!window.IsValid())
            {
                SKIP("SDL's video could not start (no display), so no window can be made");
            }
        }
    };
} // namespace WindowTests
