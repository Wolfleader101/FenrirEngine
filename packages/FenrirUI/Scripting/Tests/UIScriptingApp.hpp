#pragma once

// The UI scripting tests' part of FenrirScripting's ExtensionTestApp, shared by Roots.cpp and the generated defaults
// test (Generated/FenrirUIDefaultsTests.cpp): the fixture CTest names in FENRIR_TEST_UI_FIXTURE, assets, input and the
// UI installed (the UI installs the painter and the app's FontLibrary), and the input, assets, text, painter and UI
// extensions, the first four the ones the UI's contract builds on. Roots draw into capturing targets of a fixed size,
// so they lay out with no window; a script reaches one by its id, and what it painted is read from the captured list.

#include "ExtensionTestApp.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirPainterScripting/PainterScripting.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUIScripting/UIScripting.hpp"

namespace Fenrir::Testing
{
    // The fixture's types the tests call.
    inline constexpr const char* TreeCallsType = "UIScriptFixture.TreeCalls";
    inline constexpr const char* ClicksType = "UIScriptFixture.Clicks";
    inline constexpr const char* PaintProbeType = "UIScriptFixture.PaintProbe";

    /** @brief A 2D target of a fixed size that keeps every list a root submits to it. */
    class CaptureTarget : public UITarget
    {
      public:
        explicit CaptureTarget(UITargetSize size) : size(size)
        {
        }

        UITargetSize GetSize(App&) override
        {
            return size;
        }

        void Submit(App&, DrawList2D list) override
        {
            lists.push_back(std::move(list));
        }

        UITargetSize size;
        std::vector<DrawList2D> lists;
    };

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
