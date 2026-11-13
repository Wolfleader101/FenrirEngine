#pragma once

#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/UI.hpp"

namespace UITest
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

    // A 2D target of a fixed size that keeps every list a root submits to it: the spec's capturing test target.
    class CaptureTarget : public Fenrir::UITarget
    {
      public:
        explicit CaptureTarget(Fenrir::UITargetSize size) : size(size)
        {
        }

        Fenrir::UITargetSize GetSize(Fenrir::App&) override
        {
            return size;
        }

        void Submit(Fenrir::App&, Fenrir::DrawList2D list) override
        {
            lists.push_back(std::move(list));
        }

        Fenrir::UITargetSize size;
        std::vector<Fenrir::DrawList2D> lists;
    };

    // The window input is fed to: the roots' default window.
    inline constexpr Fenrir::WindowId testWindow{1};

    // A headless App with FenrirAssets, FenrirInput and FenrirUI and no window, driven a frame at a time; input comes
    // through FenrirInput's feed. Input is declared before the UI so the roots leave its routers before it ends.
    struct UITestApp
    {
        UITestApp() = default;

        // An app whose asset root is `assets`, logging to `logger`.
        explicit UITestApp(Fenrir::AssetsConfig assetsConfig, std::unique_ptr<Fenrir::ILogger> logger)
            : app(std::move(logger), 1), assets(Fenrir::Assets::Install(app, std::move(assetsConfig)))
        {
        }

        Fenrir::App app{std::make_unique<SilentLogger>(), 1};
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::Input input = Fenrir::Input::Install(app);
        Fenrir::UI ui = Fenrir::UI::Install(app);

        Fenrir::InputFeed& Feed()
        {
            return app.GetResource<Fenrir::InputFeed>();
        }

        // Moves the pointer to `position` in the window and clicks `button` there, all fed for the next frame.
        void Click(Fenrir::Math::Vec2 position, Fenrir::MouseButton button = Fenrir::MouseButton::Left)
        {
            Feed().MouseMove(testWindow, position, {0.0f, 0.0f});
            Feed().MouseButtonDown(testWindow, button);
            Feed().MouseButtonUp(testWindow, button);
        }

        // Presses and releases `key`, fed for the next frame.
        void Type(Fenrir::Key key, Fenrir::Scancode scancode,
                  Fenrir::KeyModifiers modifiers = Fenrir::KeyModifiers::None)
        {
            Feed().KeyDown(testWindow, key, scancode, modifiers);
            Feed().KeyUp(testWindow, key, scancode, modifiers);
        }

        Fenrir::UIRoots& Roots()
        {
            return app.GetResource<Fenrir::UIRoots>();
        }

        // A root drawing into a capture target of `logicalSize` at `contentScale`.
        Fenrir::UIRoot& MakeRoot(std::shared_ptr<CaptureTarget>& target, Fenrir::Math::Vec2 logicalSize,
                                 float contentScale = 1.0f, Fenrir::ScaleRule rule = Fenrir::ScaleRule::ContentScale)
        {
            target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{logicalSize, contentScale});
            Fenrir::UIRootConfig config;
            config.scaleRule = rule;
            config.target = target;
            return Roots().CreateRoot(config);
        }

        void Frame()
        {
            app.RunFrame(1.0 / 60.0);
        }

        // Runs `count` frames of `seconds` each: FenrirTime's frame delta, which transitions and animations step by.
        void Frames(int count, double seconds)
        {
            for (int i = 0; i < count; ++i)
            {
                app.RunFrame(seconds);
            }
        }
    };

    // The events `reader` has not read yet, copied out.
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

    inline bool Near(float a, float b, float tolerance = 1e-3f)
    {
        return std::abs(a - b) <= tolerance;
    }

    inline bool Near(const Fenrir::Rect2D& a, const Fenrir::Rect2D& b, float tolerance = 1e-3f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.width, b.width, tolerance) &&
               Near(a.height, b.height, tolerance);
    }
} // namespace UITest

namespace Catch
{
    template <>
    struct StringMaker<Fenrir::Rect2D>
    {
        static std::string convert(const Fenrir::Rect2D& rect)
        {
            return "{" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " + std::to_string(rect.width) +
                   ", " + std::to_string(rect.height) + "}";
        }
    };
} // namespace Catch
