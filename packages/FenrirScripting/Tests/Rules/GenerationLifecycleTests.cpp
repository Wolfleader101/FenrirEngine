// The generation lifecycle's rules, with no .NET: a generation is loaded from a private copy of the script assembly,
// the previous copy comes back when a new build fails, a generation that starts and fails is released, a released
// generation's copy goes only once it is collected, and a reload cannot start inside a reload. The lifecycle copies
// real files in a scratch directory of the case's own; what loads them is the fake at its loader seam, which decides
// what fails and when a generation counts as collected; and what a generation contains is a set of hooks that record
// their calls.
#include <catch2/catch_test_macros.hpp>

#include "FakeScriptAssemblyLoader.hpp"
#include "HeadlessApp.hpp"

#include "GenerationLifecycle.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace std::chrono_literals;
    using Fenrir::Detail::GenerationLifecycle;
    using Fenrir::Testing::LogLevel;
    using Result = GenerationLifecycle::Result;

    Result Ok()
    {
        return Result{Iris::CallStatus::Success, {}, false};
    }

    Result Refused(std::string why)
    {
        return Result{Iris::CallStatus::InvalidArgument, std::move(why), false};
    }

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << text;
        REQUIRE(stream.good());
    }

    // A directory of the case's own under the system temp folder: a "build output" holding a dummy script assembly,
    // the files named like it, a dll its deps file names and one it does not; and a root for the lifecycle's copies.
    struct Scratch
    {
        std::filesystem::path root;
        std::filesystem::path source;
        std::filesystem::path copies;
        std::filesystem::path assembly;

        Scratch()
        {
            static int cases = 0;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() /
                   ("FenrirScriptingRules-" + std::to_string(stamp) + "-" + std::to_string(++cases));
            source = root / "source";
            copies = root / "copies";
            assembly = source / "Script.dll";
            std::filesystem::create_directories(source);
            WriteFile(assembly, "not an assembly: the fake loader never reads it");
            WriteFile(source / "Script.pdb", "symbols");
            WriteFile(source / "Script.deps.json", R"({"dependencies": ["Helper.dll"]})");
            WriteFile(source / "Helper.dll", "a dependency the deps file names");
            WriteFile(source / "Other.dll", "a dll beside the assembly that nothing names");
        }

        ~Scratch()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;

        [[nodiscard]] std::filesystem::path Copy(int number) const
        {
            return copies / ("gen-" + std::to_string(number));
        }

        [[nodiscard]] std::set<std::string> FilesIn(const std::filesystem::path& directory) const
        {
            std::set<std::string> names;
            std::error_code ignored;
            for (const auto& entry : std::filesystem::directory_iterator(directory, ignored))
            {
                names.insert(entry.path().filename().string());
            }
            return names;
        }
    };

    // A lifecycle over the scratch directory and the fake loader, with hooks that record their calls in order and fail
    // when the case says. The unload timeout is short: the fake answers at once anyway.
    struct Lifecycle
    {
        Scratch scratch;
        Fenrir::Testing::RecordingLogger logger;
        RulesTest::FakeScriptAssemblyLoader loader;
        GenerationLifecycle generations;

        std::vector<std::string> calls;
        std::set<int> failReconciles; // The reconcile calls (counted from 1) that refuse the generation.
        std::set<int> failStarts;     // The start calls (counted from 1) that fail.
        int reconciles = 0;
        int starts = 0;
        std::function<void()> inBeforeReload; // Extra work a case does inside the beforeReload hook.

        Lifecycle()
        {
            GenerationLifecycle::Settings settings;
            settings.assembly = scratch.assembly;
            settings.scratchRoot = scratch.copies;
            settings.unloadTimeout = 1ms;
            GenerationLifecycle::Hooks hooks;
            hooks.reconcile = [this](Fenrir::Detail::LoadedScriptAssembly&) {
                calls.emplace_back("reconcile");
                return failReconciles.contains(++reconciles) ? Refused("the report is bad") : Ok();
            };
            hooks.start = [this](Fenrir::Detail::LoadedScriptAssembly&) {
                calls.emplace_back("start");
                return failStarts.contains(++starts) ? Refused("the systems could not start") : Ok();
            };
            hooks.stop = [this] { calls.emplace_back("stop"); };
            hooks.beforeReload = [this] {
                calls.emplace_back("beforeReload");
                if (inBeforeReload)
                {
                    inBeforeReload();
                }
            };
            generations.Configure(loader, &logger, std::move(settings), std::move(hooks));
        }

        [[nodiscard]] std::string Released(const std::string& why) const
        {
            return "The script assembly '" + scratch.assembly.string() + "' was released. " + why;
        }

        [[nodiscard]] std::size_t Logged(LogLevel level, const std::string& text) const
        {
            const auto messages = logger.Messages();
            return static_cast<std::size_t>(
                std::count_if(messages.begin(), messages.end(), [&](const Fenrir::Testing::LoggedMessage& message) {
                    return message.level == level && message.text.find(text) != std::string::npos;
                }));
        }

        [[nodiscard]] bool CopyExists(int number) const
        {
            return std::filesystem::is_directory(scratch.Copy(number));
        }
    };

    const std::vector<std::string> FirstLoad{"reconcile", "start"};
    const std::vector<std::string> CleanReload{"reconcile", "start", "beforeReload", "stop", "reconcile", "start"};
} // namespace

TEST_CASE("the first generation is loaded from a private copy of the assembly and what its deps file names",
          "[generation-lifecycle]")
{
    Lifecycle life;

    const auto loaded = life.generations.Load();

    INFO("The load should succeed: " + loaded.diagnostic);
    REQUIRE(loaded.Succeeded());
    INFO("One generation should be loaded, the first");
    REQUIRE((life.generations.HasGeneration() && life.generations.Generation() == 1));
    INFO("The loader should have been given the copy, not the original");
    REQUIRE(life.loader.loaded.size() == 1);
    REQUIRE(life.loader.loaded[0] == life.scratch.Copy(1) / "Script.dll");
    INFO("The copy should hold the assembly, the files named like it, and the dll its deps file names, and no other");
    REQUIRE(life.scratch.FilesIn(life.scratch.Copy(1)) ==
            std::set<std::string>{"Script.dll", "Script.pdb", "Script.deps.json", "Helper.dll"});
    INFO("The hooks should have run reconcile then start");
    REQUIRE(life.calls == FirstLoad);
}

TEST_CASE("a load the loader refuses says why and leaves no generation", "[generation-lifecycle]")
{
    Lifecycle life;
    life.loader.failLoads = {1};

    const auto loaded = life.generations.Load();

    INFO("The load should fail with the loader's reason");
    REQUIRE((!loaded.Succeeded() && loaded.status == Iris::CallStatus::InvalidArgument));
    REQUIRE(loaded.diagnostic == "The script assembly '" + life.scratch.assembly.string() +
                                     "' could not be loaded: the fake loader refused load 1");
    INFO("Nothing should be loaded and no hook should have run");
    REQUIRE((!life.generations.HasGeneration() && life.generations.Generation() == 0 && life.calls.empty()));
}

TEST_CASE("a generation reconcile refuses is released again and nothing is loaded", "[generation-lifecycle]")
{
    Lifecycle life;
    life.failReconciles = {1};

    const auto loaded = life.generations.Load();

    INFO("The load should fail with the refusal, saying the assembly was released");
    REQUIRE(!loaded.Succeeded());
    REQUIRE(loaded.diagnostic == life.Released("the report is bad"));
    INFO("The refused generation should have been released");
    REQUIRE(life.loader.released == std::vector<int>{1});
    INFO("Nothing should be loaded, and start and stop should not have run");
    REQUIRE((!life.generations.HasGeneration() && life.calls == std::vector<std::string>{"reconcile"}));
}

TEST_CASE("a generation that fails to start is stopped and released, with the start's reason", "[generation-lifecycle]")
{
    Lifecycle life;
    life.failStarts = {1};

    const auto loaded = life.generations.Load();

    INFO("The load should fail with the start's reason, saying the assembly was released");
    REQUIRE(!loaded.Succeeded());
    REQUIRE(loaded.diagnostic == life.Released("the systems could not start"));
    INFO("The generation should have been stopped and released");
    REQUIRE(life.calls == std::vector<std::string>{"reconcile", "start", "stop"});
    REQUIRE(life.loader.released == std::vector<int>{1});
    INFO("Nothing should be loaded");
    REQUIRE(!life.generations.HasGeneration());
}

TEST_CASE("a reload plays back, stops and releases the old generation, sweeps it, then loads a new copy",
          "[generation-lifecycle]")
{
    Lifecycle life;
    REQUIRE(life.generations.Load().Succeeded());

    const auto reloaded = life.generations.Reload();

    INFO("The reload should succeed: " + reloaded.diagnostic);
    REQUIRE(reloaded.Succeeded());
    INFO("The hooks should have run in the reload's order");
    REQUIRE(life.calls == CleanReload);
    INFO("The old generation should have been released and the new one loaded from a second copy");
    REQUIRE(life.loader.released == std::vector<int>{1});
    REQUIRE(life.loader.loaded.size() == 2);
    REQUIRE(life.loader.loaded[1] == life.scratch.Copy(2) / "Script.dll");
    INFO("The second generation should be the one running, with nothing left pending");
    REQUIRE((life.generations.Generation() == 2 && life.generations.PendingUnloadCount() == 0));
    INFO("The first copy should be gone once its generation was collected and nothing needs it for a restore");
    REQUIRE((!life.CopyExists(1) && life.CopyExists(2)));
}

TEST_CASE("when the new build does not load the previous copy is loaded again and the failure is reported",
          "[generation-lifecycle]")
{
    Lifecycle life;
    REQUIRE(life.generations.Load().Succeeded());
    life.loader.failLoads = {2};

    const auto reloaded = life.generations.Reload();

    INFO("The reload should report the new build's failure");
    REQUIRE((!reloaded.Succeeded() && reloaded.diagnostic.find("the fake loader refused load 2") != std::string::npos));
    INFO("The previous copy should have been loaded again");
    REQUIRE(life.loader.loaded.size() == 3);
    REQUIRE(life.loader.loaded[2] == life.scratch.Copy(1) / "Script.dll");
    REQUIRE((life.generations.HasGeneration() && life.generations.Generation() == 2));
    INFO("The restored generation should have been reconciled and started like any other");
    REQUIRE(life.calls == CleanReload);
    INFO("The failure and the restore should have been logged");
    REQUIRE((life.Logged(LogLevel::Error, "did not load") == 1 && life.Logged(LogLevel::Warn, "previous build") == 1));
}

TEST_CASE("when the new build loads but does not start the previous copy is restored", "[generation-lifecycle]")
{
    Lifecycle life;
    REQUIRE(life.generations.Load().Succeeded());
    life.failStarts = {2};

    const auto reloaded = life.generations.Reload();

    INFO("The reload should report the start's failure");
    REQUIRE((!reloaded.Succeeded() && reloaded.diagnostic == life.Released("the systems could not start")));
    INFO("The new generation should have been stopped and released, then the previous copy loaded again");
    REQUIRE(life.calls == std::vector<std::string>{"reconcile", "start", "beforeReload", "stop", "reconcile", "start",
                                                   "stop", "reconcile", "start"});
    REQUIRE(life.loader.released == std::vector<int>{1, 2});
    REQUIRE(life.loader.loaded.size() == 3);
    REQUIRE(life.loader.loaded[2] == life.scratch.Copy(1) / "Script.dll");
    INFO("A generation should be running again");
    REQUIRE(life.generations.HasGeneration());
    REQUIRE(life.Logged(LogLevel::Warn, "previous build") == 1);
}

TEST_CASE("when the previous copy cannot be restored either no generation is loaded and the log says so",
          "[generation-lifecycle]")
{
    Lifecycle life;
    REQUIRE(life.generations.Load().Succeeded());
    life.loader.failLoads = {2, 3};

    const auto reloaded = life.generations.Reload();

    INFO("The reload should report the new build's failure");
    REQUIRE(!reloaded.Succeeded());
    INFO("No generation should be loaded");
    REQUIRE(!life.generations.HasGeneration());
    INFO("The log should say the previous build could not be restored");
    REQUIRE(life.Logged(LogLevel::Error, "could not be restored either") == 1);
}

TEST_CASE("a released generation's copy is deleted only once it is collected, with one warning while it is pending",
          "[generation-lifecycle]")
{
    Lifecycle life;
    life.loader.pendingWaits = 2;
    REQUIRE(life.generations.Load().Succeeded());

    REQUIRE(life.generations.Reload().Succeeded());

    INFO("The reload's sweep should have found the old generation pending and warned once");
    REQUIRE((life.generations.PendingUnloadCount() == 1 && life.CopyExists(1)));
    REQUIRE(life.Logged(LogLevel::Warn, "has not unloaded") == 1);

    life.generations.Sweep();

    INFO("A second sweep that finds it still pending should not warn again");
    REQUIRE((life.generations.PendingUnloadCount() == 1 && life.CopyExists(1)));
    REQUIRE(life.Logged(LogLevel::Warn, "has not unloaded") == 1);

    life.generations.Sweep();

    INFO("Once collected, the generation should be forgotten and its copy deleted");
    REQUIRE((life.generations.PendingUnloadCount() == 0 && !life.CopyExists(1)));
    INFO("The running generation's copy should be untouched");
    REQUIRE(life.CopyExists(2));
}

TEST_CASE("a reload asked for from inside a reload is refused and the outer reload completes", "[generation-lifecycle]")
{
    Lifecycle life;
    REQUIRE(life.generations.Load().Succeeded());
    Iris::CallStatus inner = Iris::CallStatus::Success;
    life.inBeforeReload = [&] { inner = life.generations.Reload().status; };

    const auto reloaded = life.generations.Reload();

    INFO("The inner reload should have been refused as not initialized");
    REQUIRE(inner == Iris::CallStatus::NotInitialized);
    INFO("The outer reload should have completed: " + reloaded.diagnostic);
    REQUIRE((reloaded.Succeeded() && life.generations.Generation() == 2));
    INFO("The hooks should have run once each, as for one reload");
    REQUIRE(life.calls == CleanReload);
}

TEST_CASE("release gives back every copy and the empty scratch directory", "[generation-lifecycle]")
{
    Lifecycle life;
    life.loader.pendingWaits = 100; // The old generation is never collected while the case runs.
    REQUIRE(life.generations.Load().Succeeded());
    REQUIRE(life.generations.Reload().Succeeded());
    REQUIRE((life.CopyExists(1) && life.CopyExists(2)));

    life.generations.Release();

    INFO("No generation should be pending");
    REQUIRE(life.generations.PendingUnloadCount() == 0);
    INFO("Both copies and the directory that held them should be gone");
    REQUIRE((!life.CopyExists(1) && !life.CopyExists(2)));
    REQUIRE(!std::filesystem::exists(life.scratch.copies));
}

TEST_CASE("before it is configured the lifecycle does nothing and reports nothing loaded", "[generation-lifecycle]")
{
    GenerationLifecycle generations;

    INFO("A load should be refused as not initialized");
    REQUIRE(generations.Load().status == Iris::CallStatus::NotInitialized);
    INFO("A reload should be refused as not initialized");
    REQUIRE(generations.Reload().status == Iris::CallStatus::NotInitialized);
    INFO("A poll should be idle");
    REQUIRE(generations.Poll(std::chrono::steady_clock::now()) == GenerationLifecycle::Watch::Idle);
    generations.Unload();
    generations.Sweep();
    generations.Release();
    INFO("Nothing should be loaded or pending");
    REQUIRE((!generations.HasGeneration() && generations.Generation() == 0 && generations.PendingUnloadCount() == 0));
}
