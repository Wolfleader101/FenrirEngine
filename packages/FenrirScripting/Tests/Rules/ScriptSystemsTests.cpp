// The engine's rules for running a script's systems, with no .NET: a script with no system host has no systems; a host
// that cannot be found, listed or created fails the load; a phase the mask leaves out costs nothing; a phase asked for
// off the main thread is skipped with one error; a failing run is logged and the next phase still runs; OnDestroy runs
// once at Stop and nothing runs after it; a Start after Stop binds the new host; a stage no extension declares fails
// the load, naming the system and the stage; each extension is given its own groups, sorted, and lets go of them before
// OnDestroy, after which a kept group runs nothing. What runs inside a phase or a group (the order of systems, the
// playback, a system that throws) is C#'s, pinned by ScriptingTests through the real runtime.
#include <catch2/catch_test_macros.hpp>

#include "FakeSystemHost.hpp"
#include "HeadlessApp.hpp"

#include "ScriptSystems.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using Fenrir::Detail::ScriptSystems;
    using Fenrir::Testing::LogLevel;
    using Answer = RulesTest::FakeSystemHostLoader::Answer;
    using Names = std::vector<std::string>;

    // ScriptSystems over the fake loader and a recording logger, with a generation to hand it.
    struct Systems
    {
        Fenrir::Testing::RecordingLogger logger;
        RulesTest::FakeSystemHostLoader loader;
        ScriptSystems systems{loader, &logger};
        RulesTest::UnusedGeneration generation;

        [[nodiscard]] std::size_t Errors(const std::string& text) const
        {
            const auto messages = logger.Messages();
            return static_cast<std::size_t>(
                std::count_if(messages.begin(), messages.end(), [&](const Fenrir::Testing::LoggedMessage& message) {
                    return message.level == LogLevel::Error && message.text.find(text) != std::string::npos;
                }));
        }

        [[nodiscard]] bool AllOnThisThread() const
        {
            return std::all_of(loader.calls.begin(), loader.calls.end(),
                               [](const auto& call) { return call.thread == std::this_thread::get_id(); });
        }
    };

    const Names Started{"Find", "PhaseMask", "StageSystems", "OnCreate"};
} // namespace

TEST_CASE("a script with no system host has no systems: Start succeeds and holds nothing", "[script-systems]")
{
    Systems fixture;
    fixture.loader.answer = Answer::NoHost;

    const auto started = fixture.systems.Start(fixture.generation);

    INFO("Start should succeed: " + started.diagnostic);
    REQUIRE(started.Succeeded());
    INFO("No host should be held");
    REQUIRE(!fixture.systems.HasSystems());
    INFO("Only Find should have been asked");
    REQUIRE(fixture.loader.Names() == Names{"Find"});

    fixture.systems.RunPhase(0);
    fixture.systems.Stop();

    INFO("A phase and a stop should run nothing and count nothing");
    REQUIRE((fixture.loader.Names() == Names{"Find"} && fixture.systems.PhaseCalls() == 0));
}

TEST_CASE("a host that cannot be found, listed or created fails Start with its diagnostic and holds nothing",
          "[script-systems]")
{
    Systems fixture;
    fixture.loader.mask = 1;
    Names expectedCalls;
    std::string expectedDiagnostic;

    SECTION("the loader cannot bind the host")
    {
        fixture.loader.answer = Answer::Failure;
        expectedCalls = {"Find"};
        expectedDiagnostic = "the fake loader refused the host";
    }
    SECTION("the host cannot list its phases")
    {
        fixture.loader.failPhaseMask = true;
        expectedCalls = {"Find", "PhaseMask"};
        expectedDiagnostic = "The script's systems could not be listed: the fake host refused PhaseMask";
    }
    SECTION("the host's OnCreate fails")
    {
        fixture.loader.failOnCreate = true;
        expectedCalls = Started;
        expectedDiagnostic = "The script's systems could not start: the fake host refused OnCreate";
    }

    const auto started = fixture.systems.Start(fixture.generation);

    INFO("Start should fail with the failure's diagnostic");
    REQUIRE(!started.Succeeded());
    REQUIRE(started.diagnostic == expectedDiagnostic);
    INFO("Nothing should be held");
    REQUIRE(!fixture.systems.HasSystems());
    INFO("The host should have been asked nothing past the failure");
    REQUIRE(fixture.loader.Names() == expectedCalls);

    fixture.systems.RunPhase(0);
    fixture.systems.Stop();

    INFO("A phase and a stop after the failure should run nothing");
    REQUIRE((fixture.loader.Names() == expectedCalls && fixture.systems.PhaseCalls() == 0));
}

TEST_CASE("a phase whose mask bit is clear runs nothing, and one whose bit is set runs the host once with that phase",
          "[script-systems]")
{
    Systems fixture;
    fixture.loader.mask = 1 << 2;
    REQUIRE(fixture.systems.Start(fixture.generation).Succeeded());

    fixture.systems.RunPhase(0);
    fixture.systems.RunPhase(1);

    INFO("Phases the mask leaves out should not reach the host or be counted");
    REQUIRE((fixture.loader.Names() == Started && fixture.systems.PhaseCalls() == 0));

    fixture.systems.RunPhase(2);

    INFO("The phase the mask names should run once, with that phase, and be counted");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "Run 2"});
    REQUIRE(fixture.systems.PhaseCalls() == 1);
    INFO("Every call should have been on the thread that started the host");
    REQUIRE(fixture.AllOnThisThread());
}

TEST_CASE("a phase asked for off the main thread runs nothing and logs one error", "[script-systems]")
{
    Systems fixture;
    fixture.loader.mask = 1;
    REQUIRE(fixture.systems.Start(fixture.generation).Succeeded());

    std::thread other([&] { fixture.systems.RunPhase(0); });
    other.join();

    INFO("The host should not have been run and nothing counted");
    REQUIRE((fixture.loader.Names() == Started && fixture.systems.PhaseCalls() == 0));
    INFO("One error should say the phase was skipped");
    REQUIRE(fixture.Errors("off the main thread") == 1);

    fixture.systems.RunPhase(0);

    INFO("The same phase from the main thread should run");
    REQUIRE((fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "Run 0"} &&
             fixture.systems.PhaseCalls() == 1));
}

TEST_CASE("a failing run is logged once and the next phase still runs", "[script-systems]")
{
    Systems fixture;
    fixture.loader.mask = 0b11;
    fixture.loader.failRun = true;
    REQUIRE(fixture.systems.Start(fixture.generation).Succeeded());

    fixture.systems.RunPhase(0);

    INFO("The failure should have been logged once");
    REQUIRE(fixture.Errors("running the script's systems failed: the fake host refused Run") == 1);
    INFO("The host should still be held");
    REQUIRE(fixture.systems.HasSystems());

    fixture.systems.RunPhase(1);

    INFO("The next phase should have run and been counted");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "Run 0", "Run 1"});
    REQUIRE(fixture.systems.PhaseCalls() == 2);
}

TEST_CASE("Stop runs OnDestroy once, and a second Stop or a phase after it runs nothing", "[script-systems]")
{
    Systems fixture;
    fixture.loader.mask = 1;
    REQUIRE(fixture.systems.Start(fixture.generation).Succeeded());

    SECTION("a failing OnDestroy is logged")
    {
        fixture.loader.failOnDestroy = true;
        fixture.systems.Stop();
        INFO("The failure should have been logged once");
        REQUIRE(fixture.Errors("could not shut down cleanly: the fake host refused OnDestroy") == 1);
    }
    SECTION("a clean OnDestroy logs nothing")
    {
        fixture.systems.Stop();
        REQUIRE(fixture.Errors("") == 0);
    }

    INFO("OnDestroy should have run once and nothing should be held");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "OnDestroy"});
    REQUIRE(!fixture.systems.HasSystems());

    fixture.systems.Stop();
    fixture.systems.RunPhase(0);

    INFO("A second Stop and a phase after Stop should run nothing");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "OnDestroy"});
    REQUIRE(fixture.systems.PhaseCalls() == 0);
}

TEST_CASE("Start after Stop binds the new host", "[script-systems]")
{
    Systems fixture;
    fixture.loader.mask = 1;
    REQUIRE(fixture.systems.Start(fixture.generation).Succeeded());
    fixture.systems.RunPhase(0);
    fixture.systems.Stop();
    fixture.loader.mask = 1 << 1;

    const auto restarted = fixture.systems.Start(fixture.generation);

    INFO("The second Start should succeed: " + restarted.diagnostic);
    REQUIRE((restarted.Succeeded() && fixture.systems.HasSystems()));
    INFO("The new host should have been found, listed and created like the first");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "Run 0", "OnDestroy",
                                            "Find", "PhaseMask", "StageSystems", "OnCreate"});

    fixture.systems.RunPhase(0);
    fixture.systems.RunPhase(1);

    INFO("Phases should follow the new host's mask, and the count should carry on across generations");
    REQUIRE(fixture.loader.Names().back() == "Run 1");
    REQUIRE(fixture.systems.PhaseCalls() == 2);
}

namespace
{
    using Fenrir::Detail::StageHost;
    using Fenrir::Detail::StageSystemReport;

    // What one fake extension that hosts stages was given, and when it was told to let go, beside the host's calls.
    struct PlacedStages
    {
        std::vector<Fenrir::StageGroup> groups;
        std::optional<Fenrir::StageChanges> changes;
        int places = 0;
        int removes = 0;
    };

    StageHost FakeStageHost(std::string extension, std::vector<std::string> stages, PlacedStages& placed,
                            RulesTest::FakeSystemHostLoader& loader)
    {
        return StageHost{std::move(extension), std::move(stages),
                         [&placed](std::vector<Fenrir::StageGroup> groups, Fenrir::StageChanges changes) {
                             ++placed.places;
                             placed.groups = std::move(groups);
                             placed.changes = std::move(changes);
                         },
                         [&placed, &loader] {
                             ++placed.removes;
                             loader.calls.push_back({"Remove", std::this_thread::get_id()});
                         }};
    }
} // namespace

TEST_CASE("a stage system naming a stage no extension declares fails Start, naming the system and the stage",
          "[script-systems][stages]")
{
    Systems fixture;
    PlacedStages placed;
    fixture.systems.SetStages({FakeStageHost("bench", {"Test.Bench"}, placed, fixture.loader)}, [] {});
    fixture.loader.mask = 1;
    fixture.loader.stageSystems = {{"Game.Good", "Test.Bench", 5, 0}, {"Game.Lost", "Test.Nowhere", 5, 1}};

    const auto started = fixture.systems.Start(fixture.generation);

    INFO("Start should have failed: " + started.diagnostic);
    REQUIRE(!started.Succeeded());
    INFO("The diagnostic should name the system and the stage: " + started.diagnostic);
    REQUIRE((started.diagnostic.find("'Game.Lost'") != std::string::npos &&
             started.diagnostic.find("'Test.Nowhere'") != std::string::npos));
    INFO("Nothing of the script should have run, and nothing should be placed or held");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "StageSystems"});
    REQUIRE((placed.places == 0 && !fixture.systems.HasSystems()));
}

TEST_CASE("each extension is given its own groups, sorted, and lets go of them before OnDestroy",
          "[script-systems][stages]")
{
    Systems fixture;
    PlacedStages bench;
    PlacedStages idle;
    int playBacks = 0;
    fixture.systems.SetStages({FakeStageHost("bench", {"Test.Bench", "Test.After"}, bench, fixture.loader),
                               FakeStageHost("idle", {"Test.Idle"}, idle, fixture.loader)},
                              [&playBacks] { ++playBacks; });
    // Two systems share group 1; the script numbers groups as it likes.
    fixture.loader.stageSystems = {{"Game.Late", "Test.Bench", 20, 2},
                                   {"Game.Early", "Test.Bench", 10, 1},
                                   {"Game.EarlyToo", "Test.Bench", 10, 1},
                                   {"Game.After", "Test.After", -5, 0}};

    const auto started = fixture.systems.Start(fixture.generation);
    INFO("Start should succeed: " + started.diagnostic);
    REQUIRE(started.Succeeded());

    INFO("Each extension should be placed once, the one with no groups too");
    REQUIRE((bench.places == 1 && idle.places == 1 && idle.groups.empty()));
    INFO("The bench's groups should be one per group number, by stage name and then order");
    REQUIRE(bench.groups.size() == 3);
    REQUIRE((bench.groups[0].Stage() == "Test.After" && bench.groups[0].Order() == -5));
    REQUIRE((bench.groups[1].Stage() == "Test.Bench" && bench.groups[1].Order() == 10));
    REQUIRE((bench.groups[2].Stage() == "Test.Bench" && bench.groups[2].Order() == 20));

    bench.groups[1].Run();
    bench.groups[2].Run();
    bench.changes->PlayBack();
    INFO("A group's Run should be one call into the script, by its number, and PlayBack the session's playback");
    REQUIRE(fixture.loader.Names() ==
            Names{"Find", "PhaseMask", "StageSystems", "OnCreate", "RunStage 1", "RunStage 2"});
    REQUIRE((playBacks == 1 && fixture.systems.StageCalls() == 2));

    fixture.systems.Stop();
    INFO("Both extensions should let go of their groups, before OnDestroy");
    REQUIRE((bench.removes == 1 && idle.removes == 1));
    REQUIRE(fixture.loader.Names().back() == "OnDestroy");
    REQUIRE(fixture.loader.Names()[fixture.loader.Names().size() - 2] == "Remove");

    const auto calls = fixture.loader.calls.size();
    bench.groups[0].Run();
    bench.changes->PlayBack();
    INFO("A group or a playback kept past Stop should do nothing");
    REQUIRE((fixture.loader.calls.size() == calls && playBacks == 1));
}

TEST_CASE("a failing stage group is logged, and a group asked for off the main thread is skipped with one error",
          "[script-systems][stages]")
{
    Systems fixture;
    PlacedStages bench;
    fixture.systems.SetStages({FakeStageHost("bench", {"Test.Bench"}, bench, fixture.loader)}, [] {});
    fixture.loader.stageSystems = {{"Game.Only", "Test.Bench", 0, 0}};
    REQUIRE(fixture.systems.Start(fixture.generation).Succeeded());
    REQUIRE(bench.groups.size() == 1);

    fixture.loader.failRunStage = true;
    bench.groups[0].Run();
    INFO("The failing run should have been logged");
    REQUIRE(fixture.Errors("running the script's stage systems failed: the fake host refused RunStage") == 1);

    std::thread([&] { bench.groups[0].Run(); }).join();
    INFO("The run off the main thread should have been skipped with one error");
    REQUIRE(fixture.Errors("stage group was asked to run off the main thread") == 1);
    REQUIRE(fixture.systems.StageCalls() == 1);
    fixture.systems.Stop();
}