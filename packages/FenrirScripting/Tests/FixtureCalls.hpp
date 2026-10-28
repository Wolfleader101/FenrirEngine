#pragma once

// Calling into the fixture script assemblies from the scripting tests: the assemblies CTest names in the environment
// and the config every test starts from, the CallIn helpers and the fixture types' names, and the small readers of a
// headless app and its scene that more than one themed test source uses. The test-only extension and the native
// components the tests register are TestExtension.hpp.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace Fenrir::Testing::ScriptFixture
{
    // CTest names the assemblies it built, and the directory of this executable, in the environment.
    inline std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the scripting tests need the fixture assemblies.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    inline const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_FIXTURE");
    // The same fixture built with VARIANT2: the "new build" the reload tests swap in.
    inline const std::filesystem::path fixtureV2 = FromEnvironment("FENRIR_TEST_FIXTURE_V2");
    // The hand-written assembly whose tables report what the generator never would.
    inline const std::filesystem::path badReports = FromEnvironment("FENRIR_TEST_BAD_REPORTS");
    // The file the bad-report fixture reads its mode from (see BadReportMode).
    inline const std::filesystem::path badReportModeFile = FromEnvironment("FENRIR_BAD_REPORT_FILE");
    // The test extension's contract, the script assembly that uses it, and a contract no extension satisfies.
    inline const std::filesystem::path testExtension = FromEnvironment("FENRIR_TEST_EXTENSION");
    inline const std::filesystem::path extensionScript = FromEnvironment("FENRIR_TEST_EXTENSION_SCRIPT");
    inline const std::filesystem::path brokenExtension = FromEnvironment("FENRIR_TEST_BROKEN_EXTENSION");
    inline const std::filesystem::path executableDirectory = FromEnvironment("FENRIR_TEST_EXECUTABLE_DIR");

    constexpr int Frames = 5;

    constexpr const char* FixtureType = "Fenrir.ScriptFixture.Fixture";

    constexpr const char* AccessType = "Fenrir.ScriptFixture.Access";
    constexpr const char* RegistryType = "Fenrir.ScriptFixture.Registry";

    // Calls a static method of a type in the fixture script assembly through Iris, as the runner will call scripts.
    template <typename Result, typename... Args>
    Result CallIn(const char* typeName, Scripting& scripting, const char* method, Args... arguments)
    {
        auto* component = ScriptingProbe::Component(scripting);
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

    template <typename Result, typename... Args>
    Result Call(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(FixtureType, scripting, method, arguments...);
    }

    template <typename Result, typename... Args>
    Result Access(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(AccessType, scripting, method, arguments...);
    }

    template <typename Result, typename... Args>
    Result Registry(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(RegistryType, scripting, method, arguments...);
    }

    constexpr std::uint32_t NullEntity = 0xFFFFFFFFU;

    // Every test starts from the same config, so a field added to ScriptingConfig later is not a warning each time.
    inline ScriptingConfig ConfigFor(const std::filesystem::path& scriptAssembly)
    {
        ScriptingConfig config;
        config.scriptAssembly = scriptAssembly;
        return config;
    }

    // The name of the extension the test gives its own components and events in, as an app would.
    constexpr const char* TestAppExtension = "test app";

    // The config for `scriptAssembly` with the test's own native components and events, which it gives scripting as an
    // app does: in an extension of its own, named TestAppExtension.
    inline ScriptingConfig ConfigFor(const std::filesystem::path& scriptAssembly,
                                     std::vector<ComponentRegistration> components,
                                     std::vector<EventRegistration> events = {})
    {
        ScriptingConfig config = ConfigFor(scriptAssembly);
        ScriptingExtension app;
        app.name = TestAppExtension;
        app.components = std::move(components);
        app.events = std::move(events);
        config.extensions.push_back(std::move(app));
        return config;
    }

    constexpr const char* MathsType = "Fenrir.ScriptFixture.Maths";

    template <typename Result, typename... Args>
    Result Maths(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(MathsType, scripting, method, arguments...);
    }

    // Every error logged, for a failure message.
    inline std::string Errors(const HeadlessApp& headless)
    {
        std::string errors;
        for (const auto& message : headless.Logger().Messages())
        {
            if (message.level == LogLevel::Error)
            {
                errors += message.text + "\n";
            }
        }
        return errors;
    }

    constexpr const char* ShapeProbeType = "Fenrir.ScriptFixture.ShapeProbe";
    constexpr const char* GaugeProbeType = "Fenrir.ScriptFixture.GaugeProbe";

    constexpr const char* LinksType = "Fenrir.ScriptFixture.Links";

    template <typename Result, typename... Args>
    Result Links(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(LinksType, scripting, method, arguments...);
    }

    constexpr const char* VelocityName = "Fenrir.ScriptFixture.Velocity";
    constexpr const char* CounterName = "Fenrir.ScriptFixture.Counter";
    constexpr const char* AlignedName = "Fenrir.ScriptFixture.Aligned";
    constexpr const char* TaggedName = "Fenrir.ScriptFixture.Tagged";
    constexpr const char* ScriptedType = "Fenrir.ScriptFixture.Scripted";

    template <typename Result, typename... Args>
    Result Scripted(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ScriptedType, scripting, method, arguments...);
    }

    inline std::size_t PoolCount(EntityList& entities)
    {
        return entities.ComponentStorageCount();
    }

    constexpr const char* ChangesType = "Fenrir.ScriptFixture.Changes";

    template <typename Result, typename... Args>
    Result Changes(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ChangesType, scripting, method, arguments...);
    }

    // The script-defined Velocity, added to an entity from native code.
    inline std::uint32_t EntityWithVelocity(Scripting& scripting, EntityList& entities)
    {
        const auto id = entities.CreateEntity().GetId();
        {
            INFO("Velocity was not added");
            REQUIRE(entities.Add(scripting.FindComponent(VelocityName)->type, id));
        }
        return id;
    }

    constexpr const char* QueriesType = "Fenrir.ScriptFixture.Queries";

    template <typename Result, typename... Args>
    Result Queries(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(QueriesType, scripting, method, arguments...);
    }

    constexpr const char* ProbeType = "Fenrir.ScriptFixture.SystemProbe";

    template <typename Result, typename... Args>
    Result Probe(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ProbeType, scripting, method, arguments...);
    }

    inline std::size_t CountMessages(const HeadlessApp& headless, LogLevel level, const std::string& text)
    {
        const auto messages = headless.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const LoggedMessage& message) {
                return message.level == level && message.text.find(text) != std::string::npos;
            }));
    }

    constexpr const char* EventScriptsType = "Fenrir.ScriptFixture.EventScripts";
    constexpr const char* EventProbeType = "Fenrir.ScriptFixture.EventProbe";

    template <typename Result, typename... Args>
    Result EventScript(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(EventScriptsType, scripting, method, arguments...);
    }

    constexpr const char* StampedName = "Fenrir.ScriptFixture.Stamped";
    constexpr const char* ReloadProbeType = "Fenrir.ScriptFixture.ReloadProbe";

    template <typename Result, typename... Args>
    Result Reloaded(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ReloadProbeType, scripting, method, arguments...);
    }

    constexpr const char* BadAlphaName = "Fenrir.BadReportFixture.Alpha";
    constexpr const char* BadBeepName = "Fenrir.BadReportFixture.Beep";

    constexpr const char* ExtensionCallsType = "Fenrir.TestExtensionScript.Calls";

    template <typename Result, typename... Args>
    Result ExtensionCall(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ExtensionCallsType, scripting, method, arguments...);
    }
} // namespace Fenrir::Testing::ScriptFixture
