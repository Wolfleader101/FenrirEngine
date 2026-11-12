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

    // A headless App with FenrirAssets and FenrirUI and no window, driven a frame at a time.
    struct UITestApp
    {
        Fenrir::App app{std::make_unique<SilentLogger>(), 1};
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::UI ui = Fenrir::UI::Install(app);

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
    };

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
