#include "HeadlessApp.hpp"
#include "TestHarness.hpp"

#include "FenrirScripting/Scripting.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;

    std::filesystem::path fixture;
    std::filesystem::path executableDirectory;

    constexpr int Frames = 5;

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
        Check(!scripting.HasScripts(), "An invalid assembly loaded");
        Check(headless.Logger().Contains(LogLevel::Error, "could not be loaded"),
              "An invalid assembly was not logged as an error");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running after an invalid assembly");
    }

    void MissingBridgeIsLoggedAndTheAppRuns()
    {
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = fixture.parent_path() / "no-bridge-here";
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing bridge started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, ".NET could not be started"),
              "A missing bridge was not logged as an error");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without the bridge");
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
        {"the build deploys the bridge next to the executable", TheBuildDeploysTheBridgeNextToTheExecutable},
        {"no configured assembly does nothing and never starts .NET", NoAssemblyConfiguredDoesNothing},
        {"everything Iris owns is released when the app finishes", ShutdownReleasesEverythingWhenTheAppFinishes},
        {"destroying the owner releases everything, and a later install works",
         DestroyingTheOwnerWithoutRunningReleasesEverything},
    });
}
