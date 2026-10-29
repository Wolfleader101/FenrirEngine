#pragma once

// The input scripting tests' part of FenrirScripting's ExtensionTestApp (spec seam 7): the fixture CTest names in
// FENRIR_TEST_INPUT_FIXTURE, input installed and fed by hand through the input feed, and input's extension. C++ calls
// the fixture's static methods through Iris and compares what C# read with what C++ reads.

#include "ExtensionTestApp.hpp"

#include <cstddef>
#include <string>

#include "FenrirInput/Input.hpp"
#include "FenrirInputScripting/InputScripting.hpp"

namespace Fenrir::Testing
{
    // Frames of exactly two ticks and of none.
    inline constexpr double twoTicks = 2.0 * oneTick;
    inline constexpr double inputNoTick = 0.0;

    inline constexpr WindowId mainWindow{1};

    // The fixture's types the tests call.
    inline constexpr const char* KeysType = "Fenrir.InputScriptFixture.KeyReadings";
    inline constexpr const char* DevicesType = "Fenrir.InputScriptFixture.DeviceReads";
    inline constexpr const char* EventsType = "Fenrir.InputScriptFixture.InputEvents";
    inline constexpr const char* ShapesType = "Fenrir.InputScriptFixture.Shapes";

    struct InputPackages
    {
        explicit InputPackages(App& app) : input(Input::Install(app))
        {
        }

        Input input;
    };

    // An App with input installed and scripting running the fixture with input's extension.
    struct InputScriptingApp : ExtensionTestApp<InputPackages>
    {
        InputScriptingApp()
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_INPUT_FIXTURE"), {Input::ScriptingExtension()})
        {
        }

        InputFeed& Feed()
        {
            return headless.Get().GetResource<InputFeed>();
        }
    };

    // How many warnings the app logged that contain `text`.
    inline std::size_t WarningsWith(const HeadlessApp& headless, const std::string& text)
    {
        return LoggedWith(headless, LogLevel::Warn, text);
    }
} // namespace Fenrir::Testing
