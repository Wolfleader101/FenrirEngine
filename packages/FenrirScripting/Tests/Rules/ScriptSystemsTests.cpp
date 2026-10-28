// The engine's rules for running a script's systems, with no .NET: a script with no system host has no systems; a host
// that cannot be found, listed or created fails the load; a phase the mask leaves out costs nothing; a phase asked for
// off the main thread is skipped with one error; a failing run is logged and the next phase still runs; OnDestroy runs
// once at Stop and nothing runs after it; a Start after Stop binds the new host. What runs inside a phase (the order of
// systems, the playback after each, a system that throws) is C#'s, pinned by ScriptingTests through the real runtime.
#include <catch2/catch_test_macros.hpp>

#include "../HeadlessApp.hpp"
#include "FakeSystemHost.hpp"

#include "ScriptSystems.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <thread>
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

    const Names Started{"Find", "PhaseMask", "OnCreate"};
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
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "OnCreate", "Run 2"});
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
    REQUIRE((fixture.loader.Names() == Names{"Find", "PhaseMask", "OnCreate", "Run 0"} &&
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
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "OnCreate", "Run 0", "Run 1"});
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
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "OnCreate", "OnDestroy"});
    REQUIRE(!fixture.systems.HasSystems());

    fixture.systems.Stop();
    fixture.systems.RunPhase(0);

    INFO("A second Stop and a phase after Stop should run nothing");
    REQUIRE(fixture.loader.Names() == Names{"Find", "PhaseMask", "OnCreate", "OnDestroy"});
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
    REQUIRE(fixture.loader.Names() ==
            Names{"Find", "PhaseMask", "OnCreate", "Run 0", "OnDestroy", "Find", "PhaseMask", "OnCreate"});

    fixture.systems.RunPhase(0);
    fixture.systems.RunPhase(1);

    INFO("Phases should follow the new host's mask, and the count should carry on across generations");
    REQUIRE(fixture.loader.Names().back() == "Run 1");
    REQUIRE(fixture.systems.PhaseCalls() == 2);
}
