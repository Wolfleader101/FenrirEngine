// Hot reload: a released generation unloading and its copy deleted, a rebuilt assembly changing behaviour without a
// restart and keeping unchanged data, a changed layout dropping the component and the events, a broken build
// keeping the old generation, repeated reloads leaving nothing behind, the watcher, and reloading with nothing
// loaded.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    // A scratch directory of the test's own, removed afterwards.
    struct ScratchDirectory
    {
        std::filesystem::path path;
        ScratchDirectory()
        {
            static int counter = 0;
            path = std::filesystem::temp_directory_path() / ("fenrir-scripting-test-" + std::to_string(++counter));
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
        ~ScratchDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
        [[nodiscard]] std::size_t Entries() const
        {
            std::error_code ignored;
            std::size_t count = 0;
            if (std::filesystem::exists(path, ignored))
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ignored))
                {
                    (void)entry;
                    ++count;
                }
            }
            return count;
        }
    };

    void AReleasedGenerationUnloadsAndItsCopyIsDeleted()
    {
        ScratchDirectory scratch;
        HeadlessApp headless;
        ScriptingConfig config = ConfigFor(fixture);
        config.reloadDirectory = scratch.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("The fixture did not load as generation one");
            REQUIRE((scripting.HasScripts() && scripting.Generation() == 1));
        }
        {
            INFO("The assembly was not loaded from a copy");
            REQUIRE(scratch.Entries() > 0);
        }
        scripting.Shutdown();
        {
            INFO("The released generation did not unload");
            REQUIRE(scripting.PendingUnloadCount() == 0);
        }
        {
            INFO("The copy of the assembly was not deleted once it unloaded");
            REQUIRE(scratch.Entries() == 0);
        }
    }

    // A copy of the fixture the test may overwrite the way a build would, while an app has it loaded.
    struct WorkingFixture
    {
        ScratchDirectory directory;
        std::filesystem::path assembly;

        WorkingFixture()
        {
            std::filesystem::create_directories(directory.path / "build");
            Overwrite(fixture);
            assembly = directory.path / "build" / fixture.filename();
        }

        // Replaces the build with another one (a whole directory, as a build writes it).
        void Overwrite(const std::filesystem::path& assemblyFile) const
        {
            std::filesystem::copy(assemblyFile.parent_path(), directory.path / "build",
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing);
        }

        // A build that is not an assembly at all.
        void Break() const
        {
            std::ofstream out(directory.path / "build" / fixture.filename(), std::ios::binary | std::ios::trunc);
            out << "this is not a .NET assembly";
        }

        [[nodiscard]] ScriptingConfig Config(bool hotReload) const
        {
            ScriptingConfig config = ConfigFor(assembly);
            config.hotReload = hotReload;
            config.reloadDirectory = directory.path / "copies";
            return config;
        }

        [[nodiscard]] std::size_t Generations() const
        {
            std::size_t count = 0;
            std::error_code ignored;
            const auto copies = directory.path / "copies";
            if (!std::filesystem::exists(copies, ignored))
            {
                return 0;
            }
            for (const auto& install : std::filesystem::directory_iterator(copies, ignored))
            {
                for (const auto& generation : std::filesystem::directory_iterator(install.path(), ignored))
                {
                    (void)generation;
                    ++count;
                }
            }
            return count;
        }
    };

    std::string Join(const std::vector<std::int32_t>& values)
    {
        std::string text;
        for (const auto value : values)
        {
            text += (text.empty() ? "" : ",") + std::to_string(value);
        }
        return "[" + text + "]";
    }

    std::int32_t StampedValue(Scripting& scripting, EntityList& entities, std::uint32_t id)
    {
        const auto* values =
            static_cast<const std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, id));
        {
            INFO("The entity had no Stamped");
            REQUIRE(values != nullptr);
        }
        return values[0];
    }

    void ARebuiltAssemblyChangesBehaviourWithoutARestartAndKeepsUnchangedData()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        Scripting* installed = nullptr;
        int frame = 0;
        std::vector<std::int32_t> stampedAfterFrame;
        std::vector<std::int32_t> answers;
        std::uint32_t stamped = 0;
        std::uint32_t velocityHolder = 0;
        std::uint32_t counterHolder = 0;
        bool reloadedOk = false;

        // Before anything else in the frame (so between frames): at the start of frame four, a build lands and is
        // reloaded.
        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     if (++frame != 4)
                                     {
                                         return;
                                     }
                                     // Data to carry across: a Ping-like event that is unchanged (Echo) and one whose
                                     // layout changes (Ripple).
                                     Reloaded<void>(*installed, "SendEcho", std::int32_t{9});
                                     Reloaded<void>(*installed, "SendRipple", std::int32_t{1});
                                     (void)ScriptingProbe::ApplyChanges(*installed);
                                     answers.push_back(Reloaded<std::int32_t>(*installed, "Answer"));
                                     build.Overwrite(fixtureV2);
                                     reloadedOk = installed->Reload().Succeeded();
                                     answers.push_back(Reloaded<std::int32_t>(*installed, "Answer"));
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App&) {
                                     stampedAfterFrame.push_back(StampedValue(*installed, entities, stamped));
                                 }});
        auto scripting = Scripting::Install(app, build.Config(false));
        installed = &scripting;
        {
            INFO("The fixture did not load");
            REQUIRE((scripting.HasScripts() && scripting.Generation() == 1));
        }

        // The data to carry: a Stamped (unchanged in version 2), a Velocity (unchanged), a Counter (its layout
        // changes).
        const auto add = [&](const char* name, std::uint32_t& holder) {
            holder = entities.CreateEntity().GetId();
            {
                INFO("A component was not added");
                REQUIRE(entities.Add(scripting.FindComponent(name)->type, holder));
            }
        };
        add(StampedName, stamped);
        add(VelocityName, velocityHolder);
        add(CounterName, counterHolder);
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, stamped))[0] = 5;
        static_cast<float*>(entities.Get(scripting.FindComponent(VelocityName)->type, velocityHolder))[0] = 3.0F;
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(CounterName)->type, counterHolder))[0] = 77;

        headless.Run(6);

        {
            INFO("The new build's code did not take effect without a restart");
            REQUIRE((reloadedOk && answers == std::vector<std::int32_t>{1, 2}));
        }
        // Frames one to three added 1 each (version 1), frames four to six added 2 each (version 2): the value carried
        // over.
        {
            INFO("Stamped did not keep its value across the reload and change its step: " + Join(stampedAfterFrame));
            REQUIRE((stampedAfterFrame == std::vector<std::int32_t>{6, 7, 8, 10, 12, 14}));
        }
        // One generation said goodbye at the reload and the other at shutdown; a new set of systems was made for each
        // load.
        {
            INFO("The old systems did not say goodbye, or the new ones were not created");
            REQUIRE((CountMessages(headless, LogLevel::Info, "Creator.OnDestroy") == 2 &&
                     CountMessages(headless, LogLevel::Info, "Creator.OnCreate") == 2));
        }
        {
            INFO("The reload was not logged");
            REQUIRE(headless.Logger().Contains(LogLevel::Info, "reloaded"));
        }
        {
            INFO("Shutdown should have removed the queues");
            REQUIRE(app.FindEventQueue("Fenrir.ScriptFixture.Echo") == nullptr);
        }
    }

    void AChangedLayoutDropsTheComponentAndTheEventsAndNothingReadsStaleBytes()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        auto scripting = Scripting::Install(app, build.Config(false));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        const auto* counter = scripting.FindComponent(CounterName);
        std::vector<std::uint32_t> holders;
        for (int index = 0; index < 3; ++index)
        {
            holders.push_back(entities.CreateEntity().GetId());
            {
                INFO("A Counter was not added");
                REQUIRE(entities.Add(counter->type, holders.back()));
            }
            static_cast<std::int32_t*>(entities.Get(counter->type, holders.back()))[0] = 100 + index;
        }
        const auto stamped = entities.CreateEntity().GetId();
        {
            INFO("A Stamped was not added");
            REQUIRE(entities.Add(scripting.FindComponent(StampedName)->type, stamped));
        }
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, stamped))[0] = 41;
        {
            INFO("The test assumes version 1 of Counter is sixteen bytes");
            REQUIRE(counter->type.Size() == 16);
        }

        Reloaded<void>(scripting, "SendEcho", std::int32_t{9});
        Reloaded<void>(scripting, "SendEcho", std::int32_t{10});
        Reloaded<void>(scripting, "SendRipple", std::int32_t{1});
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The test assumes version 1 of Ripple is four bytes");
            REQUIRE(app.FindEventQueue("Fenrir.ScriptFixture.Ripple")->EventSize() == 4);
        }

        build.Overwrite(fixtureV2);
        {
            INFO("The new build did not load");
            REQUIRE((scripting.Reload().Succeeded() && scripting.Generation() == 2));
        }

        // The component whose layout changed is gone from every entity, with a warning, and is the new size.
        const auto* changed = scripting.FindComponent(CounterName);
        {
            INFO("The new Counter layout was not registered");
            REQUIRE((changed != nullptr && changed->type.Size() == 24));
        }
        {
            INFO("Old Counter bytes are still on entities");
            REQUIRE(entities.Count(changed->type) == 0);
        }
        for (const auto id : holders)
        {
            {
                INFO("An entity still has the old Counter");
                REQUIRE((!entities.Has(changed->type, id) && entities.Get(changed->type, id) == nullptr));
            }
        }
        {
            INFO("The dropped component was not reported with how many were removed");
            REQUIRE((headless.Logger().Contains(LogLevel::Warn, "'Fenrir.ScriptFixture.Counter' changed its layout") &&
                     headless.Logger().Contains(LogLevel::Warn, "3 component(s) were removed")));
        }
        // A new one starts zeroed, at the new size, rather than inheriting bytes.
        {
            INFO("A Counter of the new layout was not added");
            REQUIRE(entities.Add(changed->type, holders.front()));
        }
        const auto* fresh = static_cast<const std::uint8_t*>(entities.Get(changed->type, holders.front()));
        {
            INFO("A new Counter was not zeroed");
            REQUIRE((fresh != nullptr && std::all_of(fresh, fresh + 24, [](std::uint8_t byte) { return byte == 0; })));
        }

        // The unchanged component kept its value.
        {
            INFO("An unchanged component lost its value");
            REQUIRE(StampedValue(scripting, entities, stamped) == 41);
        }

        // The event queue whose layout is unchanged kept its events, and the one that changed was recreated empty.
        const IEventQueue* echo = app.FindEventQueue("Fenrir.ScriptFixture.Echo");
        const IEventQueue* ripple = app.FindEventQueue("Fenrir.ScriptFixture.Ripple");
        {
            INFO("An unchanged event queue lost its events");
            REQUIRE((echo != nullptr && HeldEventCount(*echo) == 2));
        }
        {
            INFO("A changed event queue was not recreated empty");
            REQUIRE((ripple != nullptr && ripple->EventSize() == 8 && HeldEventCount(*ripple) == 0));
        }
        {
            INFO("The dropped event was not reported");
            REQUIRE(headless.Logger().Contains(LogLevel::Warn, "'Fenrir.ScriptFixture.Ripple' changed its layout"));
        }
    }

    void ABrokenBuildKeepsTheOldGenerationRunning()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        Scripting* installed = nullptr;
        int frame = 0;
        bool failed = false;
        bool fixed = false;
        std::vector<std::int32_t> stampedAfterFrame;
        std::vector<std::int32_t> answers;
        std::uint32_t stamped = 0;

        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     ++frame;
                                     if (frame == 3)
                                     {
                                         build.Break();
                                         failed = !installed->Reload().Succeeded();
                                         answers.push_back(
                                             Reloaded<std::int32_t>(*installed, "Answer")); // Still version one.
                                     }
                                     if (frame == 5)
                                     {
                                         build.Overwrite(fixtureV2);
                                         fixed = installed->Reload().Succeeded();
                                         answers.push_back(Reloaded<std::int32_t>(*installed, "Answer"));
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App&) {
                                     stampedAfterFrame.push_back(StampedValue(*installed, entities, stamped));
                                 }});
        auto scripting = Scripting::Install(app, build.Config(false));
        installed = &scripting;
        stamped = entities.CreateEntity().GetId();
        {
            INFO("A Stamped was not added");
            REQUIRE(entities.Add(scripting.FindComponent(StampedName)->type, stamped));
        }

        headless.Run(6);

        {
            INFO("A build that is not an assembly was reported as loaded");
            REQUIRE(failed);
        }
        {
            INFO("The failed build and the restore were not logged");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "did not load") &&
                     headless.Logger().Contains(LogLevel::Warn, "previous build")));
        }
        // Version one kept running through the broken build: +1 a frame, no gap, no restart of the data; then version
        // two adds 2.
        {
            INFO("The old generation did not keep running through a broken build: " + Join(stampedAfterFrame));
            REQUIRE((stampedAfterFrame == std::vector<std::int32_t>{1, 2, 3, 4, 6, 8}));
        }
        {
            INFO("A later good build did not load after a broken one");
            REQUIRE(((answers == std::vector<std::int32_t>{1, 2}) && fixed));
        }
    }

    void RepeatedReloadsLeaveNoGenerationsBehind()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        Scripting* installed = nullptr;
        std::vector<std::string> problems;
        std::int32_t finalValue = 0;
        std::uint32_t stamped = 0;
        constexpr int cycles = 12;

        bool done = false;
        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     if (std::exchange(done, true))
                                     {
                                         return;
                                     }
                                     for (int cycle = 0; cycle < cycles; ++cycle)
                                     {
                                         build.Overwrite(cycle % 2 == 0 ? fixtureV2 : fixture);
                                         if (!installed->Reload().Succeeded())
                                         {
                                             problems.push_back("cycle " + std::to_string(cycle) + " did not reload");
                                         }
                                         if (installed->PendingUnloadCount() != 0)
                                         {
                                             problems.push_back("cycle " + std::to_string(cycle) +
                                                                " left a generation pending");
                                         }
                                         if (build.Generations() != 1)
                                         {
                                             problems.push_back("cycle " + std::to_string(cycle) + " left " +
                                                                std::to_string(build.Generations()) + " copies");
                                         }
                                     }
                                     finalValue = StampedValue(*installed, entities, stamped);
                                 }});
        auto scripting = Scripting::Install(app, build.Config(false));
        installed = &scripting;
        stamped = entities.CreateEntity().GetId();
        {
            INFO("A Stamped was not added");
            REQUIRE(entities.Add(scripting.FindComponent(StampedName)->type, stamped));
        }
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, stamped))[0] = 1234;

        headless.Run(2);

        {
            INFO((problems.empty() ? "" : problems.front()));
            REQUIRE(problems.empty());
        }
        {
            INFO("The generation count was off: " + std::to_string(scripting.Generation()));
            REQUIRE(scripting.Generation() == 1 + cycles);
        }
        {
            INFO("Data was lost over repeated reloads");
            REQUIRE(finalValue == 1234);
        }
        {
            INFO("A generation or its copy outlived shutdown");
            REQUIRE((scripting.PendingUnloadCount() == 0 && build.Generations() == 0));
        }
    }

    void TheWatcherReloadsAStableBuildBetweenFrames()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        int frame = 0;
        Scripting* installed = nullptr;
        std::int32_t answerAtExit = 0;
        int generationAtExit = 0;
        std::vector<int> generationByFrame;

        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     // A build that is still being written: it lands, is replaced by a broken file, and
                                     // lands again, all inside the debounce. Only the last, stable one may be loaded.
                                     ++frame;
                                     if (frame == 5 || frame == 9)
                                     {
                                         build.Overwrite(fixtureV2);
                                     }
                                     else if (frame == 7)
                                     {
                                         build.Break();
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App&) {
                                     generationByFrame.push_back(installed->Generation());
                                     std::this_thread::sleep_for(std::chrono::milliseconds(
                                         5)); // Frames take real time, so the debounce can pass.
                                 }});
        // Read before scripting is released in the Exit phase.
        app.AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                     answerAtExit = Reloaded<std::int32_t>(*installed, "Answer");
                                     generationAtExit = installed->Generation();
                                 }});
        ScriptingConfig config = build.Config(true);
        config.reloadInterval = std::chrono::milliseconds(0);
        config.reloadDebounce = std::chrono::milliseconds(60);
        auto scripting = Scripting::Install(app, config);
        installed = &scripting;

        headless.Run(80);

        {
            INFO("The watcher did not reload the new build");
            REQUIRE((answerAtExit == 2 && generationAtExit == 2));
        }
        // Not before the build landed, and only once it had been stable for the debounce.
        {
            INFO("The reload did not happen after the build, between frames");
            REQUIRE((generationByFrame.size() >= 6 && generationByFrame[3] == 1 && generationByFrame.back() == 2));
        }
        {
            INFO("One build caused more or fewer than one reload");
            REQUIRE(CountMessages(headless, LogLevel::Info, "Scripting: reloading") == 1);
        }
        {
            INFO("A build that was still changing was loaded");
            REQUIRE(CountMessages(headless, LogLevel::Error, "did not load") == 0);
        }
    }

    void WithHotReloadOffTheWatcherNeverActsButReloadStillWorks()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        int frame = 0;
        Scripting* installed = nullptr;
        int generationBeforeManualReload = 0;
        bool reloaded = false;
        int generationAtExit = 0;
        std::int32_t answerAtExit = 0;

        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     ++frame;
                                     if (frame == 2)
                                     {
                                         build.Overwrite(fixtureV2);
                                     }
                                     if (frame == 30)
                                     {
                                         generationBeforeManualReload = installed->Generation();
                                         reloaded = installed->Reload().Succeeded();
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate,
                                 {[&](App&) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }});
        app.AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                     generationAtExit = installed->Generation();
                                     answerAtExit = Reloaded<std::int32_t>(*installed, "Answer");
                                 }});
        // The watcher would look every frame and settle within 20ms, were it on.
        ScriptingConfig config = build.Config(false);
        config.reloadInterval = std::chrono::milliseconds(0);
        config.reloadDebounce = std::chrono::milliseconds(20);
        auto scripting = Scripting::Install(app, config);
        installed = &scripting;

        headless.Run(40);

        {
            INFO("The watcher reloaded a build although hot reload was off");
            REQUIRE(generationBeforeManualReload == 1);
        }
        {
            INFO("An explicit Reload did not load the new build");
            REQUIRE((reloaded && generationAtExit == 2 && answerAtExit == 2));
        }
        {
            INFO("Something other than the one explicit Reload reloaded");
            REQUIRE(CountMessages(headless, LogLevel::Info, "Scripting: reloading") == 1);
        }
    }

    void ReloadingWithNothingLoadedFailsCleanly()
    {
        // No script assembly configured: .NET never starts.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
            const auto result = scripting.Reload();
            {
                INFO("Reloading with no script assembly configured did not say so");
                REQUIRE((!result.Succeeded() && result.status == Iris::CallStatus::NotInitialized));
            }
            headless.Run(Frames);
            {
                INFO("The app did not keep running after a refused reload");
                REQUIRE(headless.FramesRun() == Frames);
            }
        }
        // A configured assembly that does not exist.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture.parent_path() / "missing.dll"));
            const auto result = scripting.Reload();
            {
                INFO("Reloading a script assembly that never loaded worked");
                REQUIRE((!result.Succeeded() && !scripting.HasScripts()));
            }
        }
    }

} // namespace

TEST_CASE("a released generation unloads and its copy is deleted", "[scripting]")
{
    AReleasedGenerationUnloadsAndItsCopyIsDeleted();
}

TEST_CASE("a rebuilt assembly changes behaviour without a restart and keeps unchanged data", "[scripting]")
{
    ARebuiltAssemblyChangesBehaviourWithoutARestartAndKeepsUnchangedData();
}

TEST_CASE("a changed layout drops the component and the events, and nothing reads stale bytes", "[scripting]")
{
    AChangedLayoutDropsTheComponentAndTheEventsAndNothingReadsStaleBytes();
}

TEST_CASE("a broken build keeps the old generation running", "[scripting]")
{
    ABrokenBuildKeepsTheOldGenerationRunning();
}

TEST_CASE("repeated reloads leave no generations behind", "[scripting]")
{
    RepeatedReloadsLeaveNoGenerationsBehind();
}

TEST_CASE("the watcher reloads a stable build between frames", "[scripting]")
{
    TheWatcherReloadsAStableBuildBetweenFrames();
}

TEST_CASE("with hot reload off the watcher never acts but Reload still works", "[scripting]")
{
    WithHotReloadOffTheWatcherNeverActsButReloadStillWorks();
}

TEST_CASE("reloading with nothing loaded fails cleanly", "[scripting]")
{
    ReloadingWithNothingLoadedFailsCleanly();
}
