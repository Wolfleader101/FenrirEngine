#pragma once

// The window binding's generated tests' (Generated/) part of FenrirScripting's ExtensionTestApp: the fixture CTest
// names in FENRIR_TEST_WINDOW_FIXTURE, no packages installed (an App with no window), and input's and the window's
// extensions.

#include "ExtensionTestApp.hpp"

#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirWindowScripting/WindowScripting.hpp"

namespace WindowScriptingTests
{
    struct WindowScriptingApp : Fenrir::Testing::ExtensionTestApp<Fenrir::Testing::NoPackages>
    {
        WindowScriptingApp()
            : ExtensionTestApp(Fenrir::Testing::FixtureFromEnvironment("FENRIR_TEST_WINDOW_FIXTURE"),
                               {Fenrir::Input::ScriptingExtension(), Fenrir::Window::ScriptingExtension()})
        {
        }
    };
} // namespace WindowScriptingTests
