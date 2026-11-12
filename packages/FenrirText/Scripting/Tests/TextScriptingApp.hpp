#pragma once

// The text scripting tests' part of FenrirScripting's ExtensionTestApp, shared by Paragraphs.cpp and the generated
// defaults test (Generated/FenrirTextDefaultsTests.cpp): the fixture CTest names in FENRIR_TEST_TEXT_FIXTURE, assets
// installed with the app's FontLibrary over their store, and the text extension. C++ calls the fixture's static
// methods through Iris and lays the same text out in the same FontLibrary to compare.

#include "ExtensionTestApp.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirTextScripting/TextScripting.hpp"

namespace Fenrir::Testing
{
    // The fixture's type the tests call.
    inline constexpr const char* ParagraphCallsType = "TextScriptFixture.ParagraphCalls";

    /**
     * @brief Assets, and the app's FontLibrary over their store, which ends first: the library unloads its atlas pages
     * from the store when it ends.
     */
    struct TextPackages
    {
        explicit TextPackages(App& app) : app(app), assets(Assets::Install(app, {}))
        {
            app.AddResource<FontLibrary>(assets.Store());
        }

        ~TextPackages()
        {
            app.RemoveResource<FontLibrary>();
        }

        TextPackages(const TextPackages&) = delete;
        TextPackages& operator=(const TextPackages&) = delete;
        TextPackages(TextPackages&&) = delete;
        TextPackages& operator=(TextPackages&&) = delete;

        App& app;
        Assets assets;

        /** @brief The app's FontLibrary, the one C# registers faces in and lays paragraphs out with. */
        FontLibrary& Fonts()
        {
            return app.GetResource<FontLibrary>();
        }
    };

    /** @brief An App with assets and a FontLibrary, and scripting running the fixture with the text extension. */
    struct TextScriptingApp : ExtensionTestApp<TextPackages>
    {
        TextScriptingApp()
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_TEXT_FIXTURE"), {Text::ScriptingExtension()})
        {
        }
    };
} // namespace Fenrir::Testing
