#pragma once

// The UI scripting tests' part of FenrirScripting's ExtensionTestApp, shared by Roots.cpp, BuildStage.cpp, Razor.cpp
// and the generated defaults test (Generated/FenrirUIDefaultsTests.cpp): the fixture CTest names in
// FENRIR_TEST_UI_FIXTURE, assets, input and the UI installed (the UI installs the painter and the app's FontLibrary),
// and the input, assets, text, painter and UI extensions, the first four the ones the UI's contract builds on. Roots
// draw into capturing targets of a fixed size, so they lay out with no window; a script reaches one by its id, and what
// it painted is read from the captured list.

#include "CaptureTarget.hpp"
#include "ExtensionTestApp.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirPainterScripting/PainterScripting.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUIScripting/UIScripting.hpp"

namespace Fenrir::Testing
{
    // The fixture's types the tests call.
    inline constexpr const char* TreeCallsType = "UIScriptFixture.TreeCalls";
    inline constexpr const char* CursorCallsType = "UIScriptFixture.CursorCalls";
    inline constexpr const char* ClicksType = "UIScriptFixture.Clicks";
    inline constexpr const char* PaintProbeType = "UIScriptFixture.PaintProbe";
    inline constexpr const char* BuildProbeType = "UIScriptFixture.BuildProbe";

    /** @brief An element id as the fixture packs it: its index in the high half, its version in the low. */
    inline std::uint64_t Packed(ElementId element)
    {
        return (static_cast<std::uint64_t>(element.index) << 32u) | element.version;
    }

    /** @brief The element id the fixture packed (see Packed). */
    inline ElementId Unpacked(std::uint64_t packed)
    {
        return ElementId{static_cast<std::uint32_t>(packed >> 32u), static_cast<std::uint32_t>(packed)};
    }

    inline bool Near(float a, float b)
    {
        return std::abs(a - b) <= 1e-3f;
    }

    // Opaque colours as the painter writes them: RGBA8, red in the lowest byte.
    inline constexpr std::uint32_t Red = 0xFF0000FFu;
    inline constexpr std::uint32_t Green = 0xFF00FF00u;
    inline constexpr std::uint32_t Blue = 0xFFFF0000u;
    inline constexpr std::uint32_t Yellow = 0xFF00FFFFu;
    inline constexpr std::uint32_t Magenta = 0xFFFF00FFu;
    inline constexpr std::uint32_t White = 0xFFFFFFFFu;

    /** @brief The list's painter vertices, copied out of its byte stream. */
    inline std::vector<PainterVertex> Vertices(const DrawList2D& list)
    {
        std::vector<PainterVertex> vertices(list.vertices.size() / sizeof(PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(PainterVertex));
        return vertices;
    }

    /** @brief Each painter quad's colour, in the order drawn: every box and every rectangle is one quad of four
     * vertices. */
    inline std::vector<std::uint32_t> QuadColours(const DrawList2D& list)
    {
        std::vector<std::uint32_t> colours;
        const std::vector<PainterVertex> vertices = Vertices(list);
        for (std::size_t i = 0; i < vertices.size(); i += 4)
        {
            colours.push_back(vertices[i].colour);
        }
        return colours;
    }

    /** @brief The centre of the list's `quad`th painter quad: the mean of its four corners. */
    inline Math::Vec2 QuadCentre(const DrawList2D& list, std::size_t quad)
    {
        const std::vector<PainterVertex> vertices = Vertices(list);
        Math::Vec2 sum(0.0f);
        for (std::size_t i = 0; i < 4; ++i)
        {
            sum += vertices[quad * 4 + i].position;
        }
        return sum / 4.0f;
    }

    /** @brief Assets, input and the UI. Input comes before the UI, so the roots leave its routers before it ends. */
    struct UIPackages
    {
        explicit UIPackages(App& app)
            : app(app), assets(Assets::Install(app, {})), input(Input::Install(app)), ui(UI::Install(app))
        {
        }

        App& app;
        Assets assets;
        Input input;
        UI ui;

        UIRoots& Roots()
        {
            return app.GetResource<UIRoots>();
        }

        /** @brief A root drawing into a capturing target of 800 by 600 logical units, one UI unit to one. */
        UIRoot& MakeRoot(std::shared_ptr<CaptureTarget>& target)
        {
            target = std::make_shared<CaptureTarget>(UITargetSize{{800.0f, 600.0f}, 1.0f});
            UIRootConfig config;
            config.scaleRule = ScaleRule::ContentScale;
            config.target = target;
            return Roots().CreateRoot(config);
        }

        InputFeed& Feed()
        {
            return app.GetResource<InputFeed>();
        }
    };

    /** @brief The text and painter extensions, and the UI's sharing the painter's state. */
    inline std::vector<ScriptingExtension> UIExtensions()
    {
        ScriptingExtension text = Text::ScriptingExtension();
        ScriptingExtension painter = Painting::ScriptingExtension(text);
        ScriptingExtension ui = UI::ScriptingExtension(painter);
        return {Input::ScriptingExtension(), Assets::ScriptingExtension(), std::move(text), std::move(painter),
                std::move(ui)};
    }

    /** @brief An App with the UI installed, and scripting running the fixture with the UI's extensions. */
    struct UIScriptingApp : ExtensionTestApp<UIPackages>
    {
        UIScriptingApp() : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_UI_FIXTURE"), UIExtensions())
        {
            INFO("The UI did not install");
            REQUIRE(ui.IsValid());
        }
    };
} // namespace Fenrir::Testing
