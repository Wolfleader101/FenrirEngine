#pragma once

// The input scripting tests' part of FenrirScripting's ExtensionTestApp (spec seam 7): the fixture CTest names in
// FENRIR_TEST_INPUT_FIXTURE, input installed and fed by hand through the input feed, and input's extension. C++ calls
// the fixture's static methods through Iris and compares what C# read with what C++ reads.

#include "ExtensionTestApp.hpp"

#include <cstddef>
#include <string>

#include "FenrirInput/Input.hpp"
#include "FenrirInputScripting/InputScripting.hpp"

namespace InputScriptingTests
{
    using Fenrir::Testing::Call;
    using Fenrir::Testing::oneTick;

    // Frames of exactly two ticks and of none.
    inline constexpr double twoTicks = 2.0 * oneTick;
    inline constexpr double noTick = 0.0;

    inline constexpr Fenrir::WindowId mainWindow{1};

    // The fixture's types the tests call.
    inline constexpr const char* KeysType = "Fenrir.InputScriptFixture.KeyReadings";
    inline constexpr const char* DevicesType = "Fenrir.InputScriptFixture.DeviceReads";
    inline constexpr const char* EventsType = "Fenrir.InputScriptFixture.InputEvents";
    inline constexpr const char* ShapesType = "Fenrir.InputScriptFixture.Shapes";

    struct InputPackages
    {
        explicit InputPackages(Fenrir::App& app) : input(Fenrir::Input::Install(app))
        {
        }

        Fenrir::Input input;
    };

    // An App with input installed and scripting running the fixture with input's extension.
    struct InputScriptingApp : Fenrir::Testing::ExtensionTestApp<InputPackages>
    {
        InputScriptingApp()
            : ExtensionTestApp(Fenrir::Testing::FixtureFromEnvironment("FENRIR_TEST_INPUT_FIXTURE"),
                               {Fenrir::Input::ScriptingExtension()})
        {
        }

        Fenrir::InputFeed& Feed()
        {
            return headless.Get().GetResource<Fenrir::InputFeed>();
        }
    };

    // How many warnings the app logged that contain `text`.
    inline std::size_t WarningsWith(const Fenrir::Testing::HeadlessApp& headless, const std::string& text)
    {
        return Fenrir::Testing::LoggedWith(headless, Fenrir::Testing::LogLevel::Warn, text);
    }
} // namespace InputScriptingTests
