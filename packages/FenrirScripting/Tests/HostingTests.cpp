// Hosting the .NET runtime and a script assembly: what loads, what a missing or broken piece does to the app, the
// engine logger and clock from C#, the shared contract, the deployed bridge, and what a scripting that is not live
// answers.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    // A directory holding copies of the files Scripting looks for beside the executable, minus the ones left out.
    struct BridgeCopy
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-bridge-copy";
        BridgeCopy(const std::filesystem::path& from, bool withBridge, bool withContract, bool withMath = true)
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
            if (withMath)
            {
                std::filesystem::copy_file(from / "Fenrir.Math.dll", path / "Fenrir.Math.dll");
            }
        }
        ~BridgeCopy()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    void RunsFramesWithTheFixture()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto messages = headless.Logger().Messages();
        {
            INFO("The runtime was not started: " +
                 (messages.empty() ? std::string("no messages") : messages.back().text));
            REQUIRE(scripting.IsRunning());
        }
        {
            INFO("The fixture assembly did not load");
            REQUIRE(scripting.HasScripts());
        }
        headless.Run(Frames);
        {
            INFO("The app did not run the requested frames");
            REQUIRE(headless.FramesRun() == Frames);
        }
        // Iris's own messages arrive through the engine logger, tagged as Iris's.
        const auto logged = headless.Logger().Messages();
        const bool fromIris = std::any_of(logged.begin(), logged.end(), [](const LoggedMessage& message) {
            return message.level == LogLevel::Info && message.text.rfind("[Iris] ", 0) == 0 &&
                   message.text.find("Fenrir.ScriptFixture") != std::string::npos;
        });
        {
            INFO("Iris's log messages did not reach the engine logger");
            REQUIRE(fromIris);
        }
    }

    void MissingAssemblyIsLoggedAndTheAppRuns()
    {
        HeadlessApp headless;
        const auto missing = fixture.parent_path() / "does-not-exist.dll";
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(missing));
        {
            INFO("A missing assembly started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing assembly was not logged as an error naming it");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "does-not-exist.dll"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without scripts");
            REQUIRE(headless.FramesRun() == Frames);
        }
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
        {
            INFO("An invalid assembly loaded, or left the runtime running with nothing to run");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("An invalid assembly was not logged as an error");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "could not be loaded"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after an invalid assembly");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void MissingBridgeIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noBridge(executableDirectory, /*withBridge=*/false, /*withContract=*/true);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noBridge.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("A missing bridge started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing bridge was not logged as an error");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, ".NET could not be started"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without the bridge");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void MissingContractIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noContract(executableDirectory, /*withBridge=*/true, /*withContract=*/false);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noContract.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("A missing contract started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing contract was not logged as an error naming it");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "Fenrir.Scripting.dll"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without the contract");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void MissingMathContractIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noMath(executableDirectory, /*withBridge=*/true, /*withContract=*/true, /*withMath=*/false);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noMath.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("A missing Fenrir.Math started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing Fenrir.Math was not logged as an error naming it");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "Fenrir.Math.dll"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without Fenrir.Math");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void ASecondActiveInstallIsRefused()
    {
        HeadlessApp first;
        auto firstScripting = Scripting::Install(first.Get(), ConfigFor(fixture));
        {
            INFO("The first install did not load");
            REQUIRE(firstScripting.HasScripts());
        }
        HeadlessApp second;
        auto secondScripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        {
            INFO("A second active install loaded scripts");
            REQUIRE(!secondScripting.HasScripts());
        }
        {
            INFO("A second install was not explained");
            REQUIRE(second.Logger().Contains(LogLevel::Error, "already active"));
        }
        second.Run(2);
        {
            INFO("The second app did not run");
            REQUIRE(second.FramesRun() == 2);
        }
        // The first is untouched, and a new install works once it is gone.
        {
            INFO("The first install stopped working");
            REQUIRE(Call<int>(firstScripting, "Answer") == 42);
        }
    }

    void ScriptsLogThroughTheEngineLogger()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        Call<void>(scripting, "LogAll");
        const auto& logger = headless.Logger();
        {
            INFO("Log.Info did not reach the engine logger");
            REQUIRE(logger.Contains(LogLevel::Info, "hello from C#"));
        }
        {
            INFO("Log.Warn did not reach the engine logger");
            REQUIRE(logger.Contains(LogLevel::Warn, "careful now"));
        }
        {
            INFO("Log.Error did not reach the engine logger");
            REQUIRE(logger.Contains(LogLevel::Error, "boom"));
        }
        // UTF-8 survives, and braces are text rather than a format string.
        {
            INFO("Non-ASCII text or braces were mangled");
            REQUIRE(logger.Contains(LogLevel::Info, "pi \xCF\x80 {0} {} {name}"));
        }
        // There is no length limit, and null is an empty message.
        {
            INFO("A long message was truncated");
            REQUIRE(logger.Contains(LogLevel::Info, std::string(2000, 'x')));
        }
        const auto messages = logger.Messages();
        {
            INFO("A null message was not logged as an empty one");
            REQUIRE(std::any_of(messages.begin(), messages.end(), [](const LoggedMessage& message) {
                return message.level == LogLevel::Info && message.text.empty();
            }));
        }
    }

    void ScriptsReadTheEngineClock()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        bool sawDelta = false;
        bool matched = true;
        headless.Get().AddSequentialSystems(SchedulePriority::Update, {[&](App& app) {
                                                const float delta = Call<float>(scripting, "DeltaTime");
                                                const float tick = Call<float>(scripting, "TickRate");
                                                matched = matched &&
                                                          delta == static_cast<float>(app.GetTime().deltaTime) &&
                                                          tick == static_cast<float>(app.GetTime().tickRate);
                                                sawDelta = sawDelta || delta > 0.0F;
                                            }});
        headless.Run(Frames);
        {
            INFO("Time did not match the engine's clock");
            REQUIRE(matched);
        }
        {
            INFO("Time.DeltaTime was never positive");
            REQUIRE(sawDelta);
        }
    }

    void EntityValidityFollowsTheActiveScene()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto isValid = [&](std::uint32_t value) { return Call<bool>(scripting, "IsValid", value); };

        const auto first = entities.CreateEntity().GetId();
        {
            INFO("A live entity was not valid");
            REQUIRE(isValid(first));
        }
        entities.DestroyEntity(first);
        {
            INFO("A destroyed entity was still valid");
            REQUIRE(!isValid(first));
        }

        // The slot is reused with a new version, so the old value stays invalid and the new one is valid.
        const auto second = entities.CreateEntity().GetId();
        {
            INFO("The test did not reuse the entity's slot with a new version");
            REQUIRE(((second & 0xFFFFFU) == (first & 0xFFFFFU) && second != first));
        }
        {
            INFO("The entity that reused the slot was not valid");
            REQUIRE(isValid(second));
        }
        {
            INFO("A stale entity became valid when its slot was reused");
            REQUIRE(!isValid(first));
        }
        {
            INFO("The null entity was valid");
            REQUIRE(!isValid(0xFFFFFFFFU));
        }
    }

    void TheContractIsOneSharedCopyAcrossGenerations()
    {
        int firstIdentity = 0;
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The contract was not the host's shared copy in the default load context");
                REQUIRE(Call<bool>(scripting, "ContractIsShared"));
            }
            firstIdentity = Call<int>(scripting, "ContractIdentity");
            // And it is the host's deployed file (beside the executable), not a copy a script brought along.
            const auto reported = Call<std::optional<std::string>>(scripting, "ContractLocation");
            {
                INFO("The contract reported no location");
                REQUIRE(reported.has_value());
            }
            const auto& location = *reported;
            {
                INFO("The contract was not loaded from the host's directory: " + location);
                REQUIRE(std::filesystem::equivalent(location, executableDirectory / "Fenrir.Scripting.dll"));
            }
        }
        // A fresh install loads the script assembly again; the contract it sees is the very same assembly.
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The contract was not shared on the second install");
            REQUIRE(Call<bool>(scripting, "ContractIsShared"));
        }
        {
            INFO("The contract changed identity between generations");
            REQUIRE(Call<int>(scripting, "ContractIdentity") == firstIdentity);
        }
    }

    void TheBuildDeploysTheBridgeNextToTheExecutable()
    {
        // iris_deploy puts these beside the executable, which is where Scripting looks by default.
        for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
        {
            {
                INFO(std::string(name) + " is not next to the executable");
                REQUIRE(std::filesystem::exists(executableDirectory / name));
            }
        }
        {
            INFO("The executable directory is empty");
            REQUIRE(!std::filesystem::is_empty(executableDirectory));
        }
    }

    void NoAssemblyConfiguredDoesNothing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
        {
            INFO("An empty config started scripting");
            REQUIRE((!scripting.IsRunning() && !scripting.HasScripts()));
        }
        headless.Run(Frames);
        {
            INFO("The app did not run");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    // What every public call answers when scripting is not live: nothing runs, nothing is recorded, nothing throws.
    void ExpectNotLive(Scripting& scripting, const char* what)
    {
        const std::string name = what;
        {
            INFO(name + " claimed to be running");
            REQUIRE((!scripting.IsRunning() && !scripting.HasScripts()));
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO(name + " applied changes");
            REQUIRE((summary.applied == 0 && summary.skipped == 0));
        }
        ScriptingProbe::DiscardChanges(scripting);
        const auto reloaded = scripting.Reload();
        {
            INFO(name + " did not refuse a reload");
            REQUIRE((!reloaded.Succeeded() && reloaded.status == Iris::CallStatus::NotInitialized));
        }
        {
            INFO(name + " reported generations waiting to unload");
            REQUIRE(scripting.PendingUnloadCount() == 0);
        }
    }

    void ScriptingThatIsNotLiveAnswersEveryCallTheSameWay()
    {
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
            ExpectNotLive(scripting, "A scripting with no assembly configured");
            {
                INFO("No assembly was reported as a generation");
                REQUIRE(scripting.Generation() == 0);
            }
        }
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture.parent_path() / "missing.dll"));
            ExpectNotLive(scripting, "A scripting whose install failed");
            {
                INFO("A failed install was reported as a generation");
                REQUIRE(scripting.Generation() == 0);
            }
        }
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The fixture did not load");
                REQUIRE(scripting.HasScripts());
            }
            const Scripting taken = std::move(scripting);
            ExpectNotLive(scripting, "A moved-from scripting"); // NOLINT(bugprone-use-after-move): that is the case
            {
                INFO("A moved-from scripting reported state");
                REQUIRE((scripting.Generation() == 0 && scripting.FindComponent("Fenrir.Transform") == nullptr));
            }
        }
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The fixture did not load");
                REQUIRE(scripting.HasScripts());
            }
            scripting.Shutdown();
            ExpectNotLive(scripting, "A shut-down scripting");
        }
    }

    void ShutdownReleasesEverythingWhenTheAppFinishes()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE((scripting.IsRunning() && scripting.HasScripts()));
        }
        headless.Run(Frames);
        // The App's Exit phase has released the script assembly and the runtime host: nothing of Iris remains
        // before the owner is even destroyed.
        {
            INFO("Iris still held the runtime after the app finished");
            REQUIRE((!scripting.IsRunning() && !scripting.HasScripts()));
        }
        scripting.Shutdown(); // Idempotent.
        {
            INFO("Shutdown twice restarted something");
            REQUIRE(!scripting.IsRunning());
        }
    }

    void DestroyingTheOwnerWithoutRunningReleasesEverything()
    {
        // Another install in the same process works after the previous one was released.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The first install did not load");
                REQUIRE(scripting.HasScripts());
            }
        }
        HeadlessApp second;
        auto scripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        {
            INFO("A second install after a destroyed one did not load");
            REQUIRE(scripting.HasScripts());
        }
        second.Run(2);
        {
            INFO("The second app did not run");
            REQUIRE(second.FramesRun() == 2);
        }
    }

} // namespace

TEST_CASE("the fixture loads, frames run and Iris's messages reach the engine logger", "[scripting]")
{
    RunsFramesWithTheFixture();
}

TEST_CASE("a missing script assembly is logged and the app keeps running", "[scripting]")
{
    MissingAssemblyIsLoggedAndTheAppRuns();
}

TEST_CASE("an invalid script assembly is logged and the app keeps running", "[scripting]")
{
    InvalidAssemblyIsLoggedAndTheAppRuns();
}

TEST_CASE("a missing bridge is logged and the app keeps running", "[scripting]")
{
    MissingBridgeIsLoggedAndTheAppRuns();
}

TEST_CASE("a missing contract is logged and the app keeps running", "[scripting]")
{
    MissingContractIsLoggedAndTheAppRuns();
}

TEST_CASE("a missing Fenrir.Math is logged and the app keeps running", "[scripting]")
{
    MissingMathContractIsLoggedAndTheAppRuns();
}

TEST_CASE("a second active install is refused and explained", "[scripting]")
{
    ASecondActiveInstallIsRefused();
}

TEST_CASE("scripts log through the engine logger at every level", "[scripting]")
{
    ScriptsLogThroughTheEngineLogger();
}

TEST_CASE("scripts read the engine clock", "[scripting]")
{
    ScriptsReadTheEngineClock();
}

TEST_CASE("entity validity follows the active scene, including slot reuse", "[scripting]")
{
    EntityValidityFollowsTheActiveScene();
}

TEST_CASE("the contract is one shared copy across generations", "[scripting]")
{
    TheContractIsOneSharedCopyAcrossGenerations();
}

TEST_CASE("the build deploys the bridge next to the executable", "[scripting]")
{
    TheBuildDeploysTheBridgeNextToTheExecutable();
}

TEST_CASE("no configured assembly does nothing and never starts .NET", "[scripting]")
{
    NoAssemblyConfiguredDoesNothing();
}

TEST_CASE("a scripting that is not live answers every call the same way", "[scripting]")
{
    ScriptingThatIsNotLiveAnswersEveryCallTheSameWay();
}

TEST_CASE("everything Iris owns is released when the app finishes", "[scripting]")
{
    ShutdownReleasesEverythingWhenTheAppFinishes();
}

TEST_CASE("destroying the owner releases everything, and a later install works", "[scripting]")
{
    DestroyingTheOwnerWithoutRunningReleasesEverything();
}
