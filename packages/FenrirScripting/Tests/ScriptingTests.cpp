#include "HeadlessApp.hpp"
#include "TestHarness.hpp"

#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/Scripting.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <type_traits>
#include <span>

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;

    std::filesystem::path fixture;
    std::filesystem::path executableDirectory;

    constexpr int Frames = 5;

    constexpr const char* FixtureType = "Fenrir.ScriptFixture.Fixture";

    constexpr const char* AccessType = "Fenrir.ScriptFixture.Access";

    // Calls a static method of a type in the fixture script assembly through Iris, as the runner will call scripts.
    template <typename Result, typename... Args>
    Result CallIn(const char* typeName, Scripting& scripting, const char* method, Args... arguments)
    {
        auto* component = scripting.GetComponent();
        Check(component != nullptr, "There is no loaded script assembly");
        const auto type = component->FindType(typeName);
        Check(type.Succeeded(), std::string("The fixture type was not found: ") + type.diagnostic);
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        Check(handle.Succeeded(), std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
        const auto result = handle.value->Invoke(arguments...);
        Check(result.Succeeded(), std::string("The fixture method ") + method + " failed: " + result.diagnostic);
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

    // The native side of the components the fixture declares (same layout as the C# structs).
    struct NativeVec3
    {
        float X, Y, Z;
    };
    struct NativeTransform
    {
        NativeVec3 Position;
        float Scale;
        std::int32_t Flags;
    };
    struct NativeNested
    {
        double Weight;
    };
    struct NativeHidden
    {
        std::int32_t Count;
    };
    struct NativeClash
    {
        std::int32_t Value, IsNull, Fine;
    };
    struct NativeSplit
    {
        std::int32_t A, B;
    };
    struct NativeReserved
    {
        std::int32_t Class, Event;
    };

    std::uint64_t AddressOf(const void* memory)
    {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory));
    }

    // A directory holding copies of the files Scripting looks for beside the executable, minus the ones left out.
    struct BridgeCopy
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-bridge-copy";
        BridgeCopy(const std::filesystem::path& from, bool withBridge, bool withContract)
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
            std::filesystem::create_directories(path);
            if (withBridge)
            {
                for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
                {
                    std::filesystem::copy_file(from / name, path / name);
                }
            }
            if (withContract)
            {
                std::filesystem::copy_file(from / "Fenrir.Scripting.dll", path / "Fenrir.Scripting.dll");
            }
        }
        ~BridgeCopy()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    // Every test starts from the same config, so a field added to ScriptingConfig later is not a warning each time.
    ScriptingConfig ConfigFor(const std::filesystem::path& scriptAssembly)
    {
        ScriptingConfig config;
        config.scriptAssembly = scriptAssembly;
        return config;
    }

    void RunsFramesWithTheFixture()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto messages = headless.Logger().Messages();
        Check(scripting.IsRunning(),
              "The runtime was not started: " + (messages.empty() ? std::string("no messages") : messages.back().text));
        Check(scripting.HasScripts(), "The fixture assembly did not load");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not run the requested frames");
        // Iris's own messages arrive through the engine logger, tagged as Iris's.
        const auto logged = headless.Logger().Messages();
        const bool fromIris = std::any_of(logged.begin(), logged.end(), [](const LoggedMessage& message) {
            return message.level == LogLevel::Info && message.text.rfind("[Iris] ", 0) == 0 &&
                   message.text.find("Fenrir.ScriptFixture") != std::string::npos;
        });
        Check(fromIris, "Iris's log messages did not reach the engine logger");
    }

    void MissingAssemblyIsLoggedAndTheAppRuns()
    {
        HeadlessApp headless;
        const auto missing = fixture.parent_path() / "does-not-exist.dll";
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(missing));
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing assembly started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, "does-not-exist.dll"),
              "A missing assembly was not logged as an error naming it");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without scripts");
    }

    void InvalidAssemblyIsLoggedAndTheAppRuns()
    {
        // Removed even when a check below throws.
        struct TempFile
        {
            std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-not-an-assembly.dll";
            TempFile()
            {
                std::ofstream(path) << "this is not a .NET assembly";
            }
            ~TempFile()
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } temp;
        const auto& invalid = temp.path;

        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(invalid));
        Check(!scripting.HasScripts() && !scripting.IsRunning(),
              "An invalid assembly loaded, or left the runtime running with nothing to run");
        Check(headless.Logger().Contains(LogLevel::Error, "could not be loaded"),
              "An invalid assembly was not logged as an error");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running after an invalid assembly");
    }

    void MissingBridgeIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noBridge(executableDirectory, /*withBridge=*/false, /*withContract=*/true);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noBridge.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing bridge started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, ".NET could not be started"),
              "A missing bridge was not logged as an error");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without the bridge");
    }

    void MissingContractIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noContract(executableDirectory, /*withBridge=*/true, /*withContract=*/false);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noContract.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing contract started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, "Fenrir.Scripting.dll"),
              "A missing contract was not logged as an error naming it");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without the contract");
    }

    void ASecondActiveInstallIsRefused()
    {
        HeadlessApp first;
        auto firstScripting = Scripting::Install(first.Get(), ConfigFor(fixture));
        Check(firstScripting.HasScripts(), "The first install did not load");
        HeadlessApp second;
        auto secondScripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        Check(!secondScripting.HasScripts(), "A second active install loaded scripts");
        Check(second.Logger().Contains(LogLevel::Error, "already active"), "A second install was not explained");
        second.Run(2);
        Check(second.FramesRun() == 2, "The second app did not run");
        // The first is untouched, and a new install works once it is gone.
        Check(Call<int>(firstScripting, "Answer") == 42, "The first install stopped working");
    }

    void ScriptsLogThroughTheEngineLogger()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        Call<void>(scripting, "LogAll");
        const auto& logger = headless.Logger();
        Check(logger.Contains(LogLevel::Info, "hello from C#"), "Log.Info did not reach the engine logger");
        Check(logger.Contains(LogLevel::Warn, "careful now"), "Log.Warn did not reach the engine logger");
        Check(logger.Contains(LogLevel::Error, "boom"), "Log.Error did not reach the engine logger");
        // UTF-8 survives, and braces are text rather than a format string.
        Check(logger.Contains(LogLevel::Info, "pi \xCF\x80 {0} {} {name}"), "Non-ASCII text or braces were mangled");
        // There is no length limit, and null is an empty message.
        Check(logger.Contains(LogLevel::Info, std::string(2000, 'x')), "A long message was truncated");
        const auto messages = logger.Messages();
        Check(std::any_of(messages.begin(), messages.end(),
                          [](const LoggedMessage& message) { return message.level == LogLevel::Info && message.text.empty(); }),
              "A null message was not logged as an empty one");
    }

    void ScriptsReadTheEngineClock()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        bool sawDelta = false;
        bool matched = true;
        headless.Get().AddSequentialSystems(SchedulePriority::Update, {[&](App& app) {
                                                const float delta = Call<float>(scripting, "DeltaTime");
                                                const float tick = Call<float>(scripting, "TickRate");
                                                matched = matched && delta == static_cast<float>(app.GetTime().deltaTime) &&
                                                          tick == static_cast<float>(app.GetTime().tickRate);
                                                sawDelta = sawDelta || delta > 0.0F;
                                            }});
        headless.Run(Frames);
        Check(matched, "Time did not match the engine's clock");
        Check(sawDelta, "Time.DeltaTime was never positive");
    }

    void EntityValidityFollowsTheActiveScene()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto isValid = [&](std::uint32_t value) { return Call<bool>(scripting, "IsValid", value); };

        const auto first = entities.CreateEntity().GetId();
        Check(isValid(first), "A live entity was not valid");
        entities.DestroyEntity(first);
        Check(!isValid(first), "A destroyed entity was still valid");

        // The slot is reused with a new version, so the old value stays invalid and the new one is valid.
        const auto second = entities.CreateEntity().GetId();
        Check((second & 0xFFFFFU) == (first & 0xFFFFFU) && second != first,
              "The test did not reuse the entity's slot with a new version");
        Check(isValid(second), "The entity that reused the slot was not valid");
        Check(!isValid(first), "A stale entity became valid when its slot was reused");
        Check(!isValid(0xFFFFFFFFU), "The null entity was valid");
    }

    void RefWritesThroughGeneratedAccessorsIntoNativeMemory()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");

        // t.Position.X += 1 on a nested struct field, a plain assignment, a compound multiply and a flag: all land in
        // memory this test owns, with no ref at the call site.
        NativeTransform transform{{1, 2, 3}, 4, 1};
        Access<void>(scripting, "Nudge", AddressOf(&transform));
        Check(transform.Position.X == 2 && transform.Position.Y == 5 && transform.Position.Z == 3 &&
                  transform.Scale == 8 && transform.Flags == 5,
              "Writes through the generated accessors did not reach native memory");
        Check(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 18.0F,
              "Reads through the generated accessors did not see native memory");

        // Writes from C++ are seen too: it is the same memory.
        transform.Position.Z = 10;
        Check(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 25.0F,
              "A native write was not seen by the script");

        NativeNested nested{1.0};
        Check(Access<double>(scripting, "NudgeNested", AddressOf(&nested)) == 1.5 && nested.Weight == 1.5,
              "A nested component type got no accessors");
        NativeHidden hidden{4};
        Check(Access<std::int32_t>(scripting, "NudgeHidden", AddressOf(&hidden)) == 7 && hidden.Count == 7,
              "An internal component got no accessors");
        NativeClash clash{0, 0, 0};
        Check(Access<std::int32_t>(scripting, "NudgeClash", AddressOf(&clash)) == 24 && clash.Fine == 7 &&
                  clash.Value == 8 && clash.IsNull == 9,
              "Fields named like Ref<T> members were not reachable through Value");
        NativeSplit split{100, 200};
        Check(Access<std::int32_t>(scripting, "NudgeSplit", AddressOf(&split)) == 303 && split.A == 101 && split.B == 202,
              "A partial component did not get accessors for all of its fields");
        NativeReserved reserved{10, 20};
        Check(Access<std::int32_t>(scripting, "NudgeReserved", AddressOf(&reserved)) == 33 && reserved.Class == 11 &&
                  reserved.Event == 22,
              "Keyword-named fields were not exposed");
    }

    void ANullRefSaysSoInsteadOfCrashing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(Access<bool>(scripting, "NullRefIsNull"), "A null Ref was not reported as null");
        const auto message = Access<std::optional<std::string>>(scripting, "NullRefMessage");
        Check(message.has_value() && message->find("Transform") != std::string::npos,
              "Reading a null Ref did not throw a clear exception");
    }

    void TheContractIsOneSharedCopyAcrossGenerations()
    {
        int firstIdentity = 0;
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            Check(Call<bool>(scripting, "ContractIsShared"),
                  "The contract was not the host's shared copy in the default load context");
            firstIdentity = Call<int>(scripting, "ContractIdentity");
            // And it is the host's deployed file (beside the executable), not a copy a script brought along.
            const auto reported = Call<std::optional<std::string>>(scripting, "ContractLocation");
            Check(reported.has_value(), "The contract reported no location");
            const auto& location = *reported;
            Check(std::filesystem::equivalent(location, executableDirectory / "Fenrir.Scripting.dll"),
                  "The contract was not loaded from the host's directory: " + location);
        }
        // A fresh install loads the script assembly again; the contract it sees is the very same assembly.
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(Call<bool>(scripting, "ContractIsShared"), "The contract was not shared on the second install");
        Check(Call<int>(scripting, "ContractIdentity") == firstIdentity,
              "The contract changed identity between generations");
    }

    void TheBuildDeploysTheBridgeNextToTheExecutable()
    {
        // iris_deploy puts these beside the executable, which is where Scripting looks by default.
        for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
        {
            Check(std::filesystem::exists(executableDirectory / name), std::string(name) + " is not next to the executable");
        }
        Check(!std::filesystem::is_empty(executableDirectory), "The executable directory is empty");
    }

    void NoAssemblyConfiguredDoesNothing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
        Check(!scripting.IsRunning() && !scripting.HasScripts(), "An empty config started scripting");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not run");
    }

    void ShutdownReleasesEverythingWhenTheAppFinishes()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.IsRunning() && scripting.HasScripts(), "The fixture did not load");
        headless.Run(Frames);
        // The App's Exit phase has released the script assembly and the runtime host: nothing of Iris remains
        // before the owner is even destroyed.
        Check(!scripting.IsRunning() && !scripting.HasScripts(), "Iris still held the runtime after the app finished");
        scripting.Shutdown(); // Idempotent.
        Check(!scripting.IsRunning(), "Shutdown twice restarted something");
    }

    void DestroyingTheOwnerWithoutRunningReleasesEverything()
    {
        // Another install in the same process works after the previous one was released.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            Check(scripting.HasScripts(), "The first install did not load");
        }
        HeadlessApp second;
        auto scripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "A second install after a destroyed one did not load");
        second.Run(2);
        Check(second.FramesRun() == 2, "The second app did not run");
    }
} // namespace

int main(int argc, char* argv[])
{
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2)
    {
        std::cerr << "Usage: FenrirScriptingTests Fenrir.ScriptFixture.dll\n";
        return EXIT_FAILURE;
    }
    fixture = std::filesystem::absolute(arguments[1]);
    executableDirectory = std::filesystem::absolute(arguments[0]).parent_path();
    return RunCases({
        {"the fixture loads, frames run and Iris's messages reach the engine logger", RunsFramesWithTheFixture},
        {"a missing script assembly is logged and the app keeps running", MissingAssemblyIsLoggedAndTheAppRuns},
        {"an invalid script assembly is logged and the app keeps running", InvalidAssemblyIsLoggedAndTheAppRuns},
        {"a missing bridge is logged and the app keeps running", MissingBridgeIsLoggedAndTheAppRuns},
        {"a missing contract is logged and the app keeps running", MissingContractIsLoggedAndTheAppRuns},
        {"a second active install is refused and explained", ASecondActiveInstallIsRefused},
        {"scripts log through the engine logger at every level", ScriptsLogThroughTheEngineLogger},
        {"scripts read the engine clock", ScriptsReadTheEngineClock},
        {"entity validity follows the active scene, including slot reuse", EntityValidityFollowsTheActiveScene},
        {"the contract is one shared copy across generations", TheContractIsOneSharedCopyAcrossGenerations},
        {"Ref<T> writes through generated accessors into native memory", RefWritesThroughGeneratedAccessorsIntoNativeMemory},
        {"a null Ref says so instead of crashing", ANullRefSaysSoInsteadOfCrashing},
        {"the build deploys the bridge next to the executable", TheBuildDeploysTheBridgeNextToTheExecutable},
        {"no configured assembly does nothing and never starts .NET", NoAssemblyConfiguredDoesNothing},
        {"everything Iris owns is released when the app finishes", ShutdownReleasesEverythingWhenTheAppFinishes},
        {"destroying the owner releases everything, and a later install works",
         DestroyingTheOwnerWithoutRunningReleasesEverything},
    });
}
