#pragma once

// What the window binding's generated tests (Generated/) run in: an App with no window and scripting running the window
// fixture script assembly with input's and the window's extensions. CTest names the fixture in
// FENRIR_TEST_WINDOW_FIXTURE.

#include "HeadlessApp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirWindowScripting/WindowScripting.hpp"

namespace WindowScriptingTests
{
    struct WindowScriptingApp
    {
        WindowScriptingApp() : scripting(Fenrir::Scripting::Install(headless.Get(), Config()))
        {
            INFO("The window fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        Fenrir::Testing::HeadlessApp headless;
        Fenrir::Scripting scripting;

      private:
        static Fenrir::ScriptingConfig Config()
        {
            const char* fixture = std::getenv("FENRIR_TEST_WINDOW_FIXTURE");
            INFO("Set FENRIR_TEST_WINDOW_FIXTURE (CTest does): the window scripting tests need the fixture assembly");
            REQUIRE(fixture != nullptr);
            Fenrir::ScriptingConfig config;
            config.scriptAssembly = std::filesystem::absolute(fixture);
            config.hotReload = false;
            config.extensions = {Fenrir::Input::ScriptingExtension(), Fenrir::Window::ScriptingExtension()};
            return config;
        }
    };
} // namespace WindowScriptingTests
