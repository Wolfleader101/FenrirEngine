#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "CaptureTarget.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirWindow/Window.hpp"

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

    // The capturing test target, from FenrirUI's test support.
    using Fenrir::Testing::CaptureTarget;

    // The window input is fed to: the roots' default window.
    inline constexpr Fenrir::WindowId testWindow{1};

    // A headless App with FenrirAssets, FenrirInput and FenrirUI and no window, driven a frame at a time; input comes
    // through FenrirInput's feed. Input is declared before the UI so the roots leave its routers before it ends.
    struct UITestApp
    {
        UITestApp() = default;

        // An app whose asset root is `assetsConfig`'s.
        explicit UITestApp(Fenrir::AssetsConfig assetsConfig)
            : assets(Fenrir::Assets::Install(app, std::move(assetsConfig)))
        {
        }

        // An app whose asset root is `assetsConfig`'s, logging to `logger`.
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

    // A UITestApp with a hidden 320 by 240 window installed before the rest, for what needs FenrirWindow: its
    // clipboard, or the cursor shape the roots set on it.
    struct WindowUITestApp
    {
        Fenrir::App app{std::make_unique<SilentLogger>(), 1};
        Fenrir::Window window = Fenrir::Window::Install(app, HiddenWindow());
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::Input input = Fenrir::Input::Install(app);
        Fenrir::UI ui = Fenrir::UI::Install(app);

        static Fenrir::WindowConfig HiddenWindow()
        {
            Fenrir::WindowConfig config;
            config.title = "FenrirUITests";
            config.logicalSize = {320, 240};
            config.visible = false;
            return config;
        }

        // Skips the calling test when SDL's video could not start: with no display there is no window, so no
        // `missing` (what the test needs from it).
        void RequireDisplay(std::string_view missing) const
        {
            if (!window.IsValid())
            {
                SKIP("SDL's video could not start (no display), so there is no " << missing);
            }
        }

        Fenrir::InputFeed& Feed()
        {
            return app.GetResource<Fenrir::InputFeed>();
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

        void Frame()
        {
            app.RunFrame(1.0 / 60.0);
        }
    };

    // A UITestApp with one root over an 800 by 600 capture target, for tests that read elements' resolved style.
    struct StyleTest
    {
        UITestApp test;
        std::shared_ptr<CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

        // A panel under `parent` with the classes `classes`, space-separated.
        Fenrir::ElementId Make(Fenrir::ElementId parent, std::string_view classes)
        {
            const Fenrir::ElementId element = root.Create(parent, "panel");
            for (std::size_t start = 0; start < classes.size();)
            {
                const std::size_t end = std::min(classes.find(' ', start), classes.size());
                root.AddClass(element, classes.substr(start, end - start));
                start = end + 1;
            }
            return element;
        }

        Fenrir::ElementId Make(std::string_view classes)
        {
            return Make(root.GetRootElement(), classes);
        }

        const Fenrir::ResolvedStyle& Style(Fenrir::ElementId element) const
        {
            const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
            INFO("the element is alive and styled");
            REQUIRE(style != nullptr);
            return *style;
        }

        // Sets `name: value` inline on `element`, which must read.
        void Set(Fenrir::ElementId element, std::string_view name, std::string_view value)
        {
            INFO("the inline declaration " << name << ": " << value << " reads");
            REQUIRE(root.SetStyleProperty(element, name, value));
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

    // A colour packed as the painter's vertices hold it: red in the low byte.
    constexpr std::uint32_t Rgba8(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
    {
        return r | (g << 8u) | (b << 16u) | (a << 24u);
    }

    // Every vertex of `list`, in the order drawn.
    inline std::vector<Fenrir::PainterVertex> Vertices(const Fenrir::DrawList2D& list)
    {
        std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
        return vertices;
    }

    // The box the `quad`-th quad of `vertices` draws: its shape's centre and half size, without the quad's margin.
    inline Fenrir::Rect2D QuadBox(const std::vector<Fenrir::PainterVertex>& vertices, std::size_t quad)
    {
        Fenrir::Math::Vec2 centre(0.0f);
        for (std::size_t i = 0; i < 4; ++i)
        {
            centre += vertices[quad * 4 + i].position;
        }
        centre /= 4.0f;
        const Fenrir::Math::Vec2 half = vertices[quad * 4].halfSize;
        return {centre.x - half.x, centre.y - half.y, half.x * 2.0f, half.y * 2.0f};
    }

    // A white image `width` by `height` in the app's asset store, under `name` for a stylesheet's url().
    inline Fenrir::AssetHandle<Fenrir::Image> AddWhiteImage(Fenrir::App& app, std::string_view name,
                                                            std::uint32_t width, std::uint32_t height)
    {
        Fenrir::ImageData pixels;
        pixels.width = width;
        pixels.height = height;
        pixels.pixels.assign(static_cast<std::size_t>(width) * height * 4, std::byte{0xFF});
        return app.GetResource<Fenrir::AssetStore>().AddBuiltin<Fenrir::Image>(name, std::move(pixels));
    }

    // Each quad's first vertex, in the order drawn: every painter box is one quad of four.
    inline std::vector<Fenrir::PainterVertex> Quads(const Fenrir::DrawList2D& list)
    {
        const std::vector<Fenrir::PainterVertex> vertices = Vertices(list);
        std::vector<Fenrir::PainterVertex> quads;
        for (std::size_t i = 0; i < vertices.size(); i += 4)
        {
            quads.push_back(vertices[i]);
        }
        return quads;
    }

    // Every quad's colour, in the order drawn.
    inline std::vector<std::uint32_t> Colours(const Fenrir::DrawList2D& list)
    {
        std::vector<std::uint32_t> colours;
        for (const Fenrir::PainterVertex& quad : Quads(list))
        {
            colours.push_back(quad.colour);
        }
        return colours;
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

    // Each channel within `tolerance`: for a colour mixed or converted on the way, as a transition's is.
    inline bool Near(const Fenrir::Colour& a, const Fenrir::Colour& b, float tolerance = 1e-3f)
    {
        return Near(a.r, b.r, tolerance) && Near(a.g, b.g, tolerance) && Near(a.b, b.b, tolerance) &&
               Near(a.a, b.a, tolerance);
    }

    // Every channel equal: for a colour that reaches the style unmixed, as a named colour does.
    inline bool Same(const Fenrir::Colour& a, const Fenrir::Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
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
