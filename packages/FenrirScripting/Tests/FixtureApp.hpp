#pragma once

// The harness of the built-in binding's generated tests (Generated/FenrirScriptingDefaultsTests.cpp and
// Generated/FenrirScriptingNativesTests.cpp): an App with no window and scripting running the fixture script assembly,
// which compiles the tests' C# classes.

#include "HeadlessApp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "FenrirScripting/Scripting.hpp"

namespace Fenrir::Testing
{
    /**
     * @brief An App with scripting running the fixture CTest names in FENRIR_TEST_FIXTURE. Members end in reverse
     * order: scripting, then the app.
     */
    struct FixtureApp
    {
        FixtureApp() : scripting(Scripting::Install(headless.Get(), Config()))
        {
            INFO("The fixture script assembly did not load");
            REQUIRE(scripting.HasScripts());
        }

        static ScriptingConfig Config()
        {
            const char* fixture = std::getenv("FENRIR_TEST_FIXTURE");
            if (fixture == nullptr)
            {
                std::cerr << "Set FENRIR_TEST_FIXTURE (CTest does): the generated tests need the fixture assembly.\n";
            }
            ScriptingConfig config;
            config.scriptAssembly = fixture == nullptr ? std::filesystem::path() : std::filesystem::absolute(fixture);
            config.hotReload = false;
            return config;
        }

        HeadlessApp headless;
        Scripting scripting;
    };
} // namespace Fenrir::Testing
