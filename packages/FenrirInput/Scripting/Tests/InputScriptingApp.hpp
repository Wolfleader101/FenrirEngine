#pragma once

// The input scripting tests' harness (spec seam 7): an App with no window, input fed by hand through the input feed,
// stepped one frame at a time, with scripting running the input fixture script assembly, which uses the Fenrir.Input
// contract. C++ calls the fixture's static methods through Iris, as the system runner calls scripts, and compares what
// C# read with what C++ reads.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>

#include "FenrirInput/Input.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace InputScriptingTests
{
    inline std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the input scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    inline const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_INPUT_FIXTURE");

    // The App's default fixed step: a frame of `oneTick` seconds runs exactly one tick, two of them exactly two.
    inline constexpr double oneTick = Fenrir::Testing::HeadlessApp::FrameDelta;
    inline constexpr double twoTicks = 2.0 * oneTick;
    inline constexpr double noTick = 0.0;

    inline constexpr Fenrir::WindowId mainWindow{1};

    // The fixture's types the tests call.
    inline constexpr const char* KeysType = "Fenrir.InputScriptFixture.KeyReadings";
    inline constexpr const char* DevicesType = "Fenrir.InputScriptFixture.DeviceReads";
    inline constexpr const char* EventsType = "Fenrir.InputScriptFixture.InputEvents";
    inline constexpr const char* ShapesType = "Fenrir.InputScriptFixture.Shapes";

    // Calls a static method of a type in the loaded fixture through Iris.
    template <typename Result, typename... Args>
    Result Call(Fenrir::Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        auto* component = Fenrir::ScriptingProbe::Component(scripting);
        {
            INFO("There is no loaded script assembly");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(typeName);
        {
            INFO(std::string("The fixture type was not found: ") + type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        {
            INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const auto result = handle.value->Invoke(arguments...);
        {
            INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    // Scripting running the fixture, with input's extension.
    inline Fenrir::ScriptingConfig FixtureConfig()
    {
        Fenrir::ScriptingConfig config;
        config.scriptAssembly = fixture;
        config.hotReload = false;
        config.extensions = {Fenrir::Input::ScriptingExtension()};
        return config;
    }

    // An App with input installed and scripting running the fixture with input's extension. Members end in reverse
    // order: scripting, then input, then the app.
    struct InputScriptingApp
    {
        InputScriptingApp()
            : input(Fenrir::Input::Install(headless.Get())),
              scripting(Fenrir::Scripting::Install(headless.Get(), FixtureConfig()))
        {
            INFO("The fixture did not load with input's extension");
            REQUIRE(scripting.HasScripts());
        }

        Fenrir::Testing::HeadlessApp headless;
        Fenrir::Input input;
        Fenrir::Scripting scripting;

        Fenrir::App& Get()
        {
            return headless.Get();
        }

        Fenrir::InputFeed& Feed()
        {
            return headless.Get().GetResource<Fenrir::InputFeed>();
        }

        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            return InputScriptingTests::Call<Result>(scripting, typeName, method, arguments...);
        }
    };

    // How many warnings the app logged that contain `text`.
    inline std::size_t WarningsWith(const Fenrir::Testing::HeadlessApp& headless, const std::string& text)
    {
        std::size_t count = 0;
        for (const Fenrir::Testing::LoggedMessage& message : headless.Logger().Messages())
        {
            count += message.level == Fenrir::Testing::LogLevel::Warn && message.text.find(text) != std::string::npos
                         ? 1
                         : 0;
        }
        return count;
    }
} // namespace InputScriptingTests
