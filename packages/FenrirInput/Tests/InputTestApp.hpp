#pragma once

#include <memory>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Input.hpp"

namespace InputTests
{
    // A logger that drops everything: a second ConsoleLogger in one process throws.
    class SilentLogger : public Fenrir::ILogger
    {
      public:
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
        void ErrorImpl(std::string_view) override
        {
        }
        void FatalImpl(std::string_view) override
        {
        }
    };

    // The App's default fixed step: a frame of `oneTick` seconds runs exactly one tick, two of them exactly two.
    inline constexpr double oneTick = 1.0 / 60.0;
    inline constexpr double twoTicks = 2.0 * oneTick;
    inline constexpr double noTick = 0.0;

    inline constexpr Fenrir::WindowId mainWindow{1};
    inline constexpr Fenrir::WindowId otherWindow{2};

    // An App with input installed and nothing else, driven one frame at a time.
    struct InputTestApp
    {
        Fenrir::App app{std::make_unique<SilentLogger>(), 2};
        Fenrir::Input input = Fenrir::Input::Install(app);

        Fenrir::InputFeed& Feed()
        {
            return app.GetResource<Fenrir::InputFeed>();
        }

        const Fenrir::Keyboard& Keyboard()
        {
            return app.GetResource<Fenrir::Keyboard>();
        }

        const Fenrir::Mouse& Mouse()
        {
            return app.GetResource<Fenrir::Mouse>();
        }

        const Fenrir::Gamepads& Gamepads()
        {
            return app.GetResource<Fenrir::Gamepads>();
        }

        // Feeds `key` going down, at the scancode of the same name.
        void Press(Fenrir::Key key, Fenrir::Scancode scancode)
        {
            Feed().KeyDown(mainWindow, key, scancode);
        }

        void Release(Fenrir::Key key, Fenrir::Scancode scancode)
        {
            Feed().KeyUp(mainWindow, key, scancode);
        }
    };
} // namespace InputTests
