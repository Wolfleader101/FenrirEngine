#pragma once

// The painter scripting tests' part of FenrirScripting's ExtensionTestApp, shared by Paints.cpp and the generated
// defaults test (Generated/FenrirPainterDefaultsTests.cpp): the fixture CTest names in FENRIR_TEST_PAINTER_FIXTURE,
// assets, the renderer on the null backend (for its window overlay), the painter's shader and the app's FontLibrary
// installed, and the input, assets, text and painter extensions, the first three the ones the painter's contract builds
// on. What C# painted is read back from the renderer's `Overlay 2D` pass through RendererProbe.

#include "ExtensionTestApp.hpp"

#include <string_view>
#include <variant>
#include <vector>

#include "Device/CommandList.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirPainter/Painting.hpp"
#include "FenrirPainterScripting/PainterScripting.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "RendererProbe.hpp"

namespace Fenrir::Testing
{
    // The fixture's type the tests call.
    inline constexpr const char* PaintCallsType = "PainterScriptFixture.PaintCalls";

    /** @brief The renderer on the null backend, which records what it would draw. */
    inline RendererConfig NullRenderer()
    {
        RendererConfig config;
        config.backend = GraphicsBackend::Null;
        return config;
    }

    /**
     * @brief Assets, the renderer, the painter's shader, and the app's FontLibrary over the store, which ends first:
     * the library unloads its atlas pages from the store when it ends.
     */
    struct PainterPackages
    {
        explicit PainterPackages(App& app)
            : app(app), assets(Assets::Install(app, {})), renderer(Renderer::Install(app, NullRenderer())),
              painting(Painting::Install(app))
        {
            app.AddResource<FontLibrary>(assets.Store());
        }

        ~PainterPackages()
        {
            app.RemoveResource<FontLibrary>();
        }

        PainterPackages(const PainterPackages&) = delete;
        PainterPackages& operator=(const PainterPackages&) = delete;
        PainterPackages(PainterPackages&&) = delete;
        PainterPackages& operator=(PainterPackages&&) = delete;

        App& app;
        Assets assets;
        Renderer renderer;
        Painting painting;
    };

    /** @brief The text extension, and the painter's sharing its state, after the extensions their contracts use. */
    inline std::vector<ScriptingExtension> PainterExtensions()
    {
        ScriptingExtension text = Text::ScriptingExtension();
        ScriptingExtension painter = Painting::ScriptingExtension(text);
        return {Input::ScriptingExtension(), Assets::ScriptingExtension(), std::move(text), std::move(painter)};
    }

    /** @brief An App with the painter installed, and scripting running the fixture with the painter's extensions. */
    struct PainterScriptingApp : ExtensionTestApp<PainterPackages>
    {
        PainterScriptingApp()
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_PAINTER_FIXTURE"), PainterExtensions())
        {
            INFO("The renderer did not install on the null backend");
            REQUIRE(renderer.IsValid());
        }

        /** @brief Runs a frame: every indexed draw of the `Overlay 2D` pass in the command lists it submits. */
        std::vector<Graphics::CmdDrawIndexed> OverlayDraws()
        {
            Frame();
            std::vector<Graphics::CmdDrawIndexed> draws;
            for (const Graphics::CommandList& list : RendererProbe::Submitted(renderer))
            {
                bool inPass = false;
                for (const Graphics::Command& command : list.commands)
                {
                    if (const auto* begin = std::get_if<Graphics::CmdBeginPass>(&command))
                    {
                        inPass = begin->pass.label == std::string_view("Overlay 2D");
                    }
                    else if (const auto* draw = std::get_if<Graphics::CmdDrawIndexed>(&command);
                             draw != nullptr && inPass)
                    {
                        draws.push_back(*draw);
                    }
                }
            }
            return draws;
        }
    };
} // namespace Fenrir::Testing
