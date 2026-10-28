// C# systems in the App's schedule: phase and Order, changes applied after each, Tick once per fixed step, a thrower
// dropped and logged once, OnCreate and OnDestroy, and nothing after Shutdown.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    std::vector<std::string> SplitEvents(const std::string& dump)
    {
        std::vector<std::string> events;
        std::string current;
        for (const char character : dump)
        {
            if (character == ',')
            {
                events.push_back(current);
                current.clear();
            }
            else
            {
                current += character;
            }
        }
        if (!current.empty())
        {
            events.push_back(current);
        }
        return events;
    }

    void SystemsRunInPhaseAndOrderAndChangesApplyAfterEachOne()
    {
        constexpr int frames = 4;
        HeadlessApp headless;
        int nativeTicks = 0;
        headless.Get().AddSequentialSystems(SchedulePriority::Tick, {[&](App&) { ++nativeTicks; }});
        // Scripting is released in the Exit phase, so what the systems did is read by an Exit system registered first.
        Scripting* installed = nullptr;
        std::string dump;
        headless.Get().AddSequentialSystems(
            SchedulePriority::Exit,
            {[&](App&) { dump = Probe<std::optional<std::string>>(*installed, "Dump").value_or(""); }});
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        installed = &scripting;
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        Probe<void>(scripting, "Enable");
        headless.Run(frames);

        const auto events = SplitEvents(dump);
        std::vector<std::string> withoutTicks;
        std::size_t scriptTicks = 0;
        std::string previous;
        for (const auto& event : events)
        {
            if (event == "Tick")
            {
                ++scriptTicks;
                // Fixed steps happen after PreUpdate and before Update, never anywhere else in the frame.
                {
                    INFO("A Tick ran outside the gap between PreUpdate and Update");
                    REQUIRE((previous == "Pre" || previous == "Tick"));
                }
            }
            else
            {
                withoutTicks.push_back(event);
            }
            previous = event;
        }
        // Each frame: PreUpdate, then Update in Order (Lead 1 although its name sorts last, Creator 5 makes an entity,
        // Early 10 already sees it, TieA and TieB 12 by name, Thrower 15 throws on frame one and is dropped, Late 20,
        // Observer 30), then PostUpdate. The thrower's entity never appears.
        std::vector<std::string> expected;
        for (int frame = 1; frame <= frames; ++frame)
        {
            for (const auto& event :
                 {std::string("Pre"), std::string("Lead"), "Early=" + std::to_string(frame), std::string("TieA"),
                  std::string("TieB"), std::string("Late"), "Seen=" + std::to_string(frame), std::string("Post")})
            {
                expected.push_back(event);
            }
        }
        {
            INFO("The systems did not run in phase and Order, with changes applied after each");
            REQUIRE(withoutTicks == expected);
        }

        // Tick runs once per fixed step, as often as a native Tick system does.
        {
            INFO("Tick did not run once per fixed step");
            REQUIRE((scriptTicks == static_cast<std::size_t>(nativeTicks) && nativeTicks > 0));
        }

        // One transition per phase that has systems, per frame, and per fixed step for Tick.
        {
            INFO("The engine did not enter the script exactly once per phase");
            REQUIRE(ScriptingProbe::PhaseCalls(scripting) == static_cast<std::size_t>(3 * frames + nativeTicks));
        }

        // The thrower was logged once with its name, and the others kept running.
        {
            INFO("A throwing system was not logged exactly once");
            REQUIRE(CountMessages(headless, LogLevel::Error, "ThrowerSystem") == 1);
        }
        {
            INFO("The exception's message was not logged");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "kaboom"));
        }
    }

    void OnCreateRunsAfterLoadAndOnDestroyBeforeRelease()
    {
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("OnCreate did not run after load");
                REQUIRE(headless.Logger().Contains(LogLevel::Info, "Creator.OnCreate"));
            }
            {
                INFO("OnDestroy ran too early");
                REQUIRE(!headless.Logger().Contains(LogLevel::Info, "Creator.OnDestroy"));
            }
            headless.Run(2); // The Exit phase releases scripting.
            {
                INFO("OnDestroy did not run at shutdown");
                REQUIRE(headless.Logger().Contains(LogLevel::Info, "Creator.OnDestroy"));
            }
            {
                INFO("Scripting was still running after the app finished");
                REQUIRE(!scripting.IsRunning());
            }
        }
        {
            // An explicit Shutdown says goodbye too, and only once.
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            scripting.Shutdown();
            scripting.Shutdown();
            {
                INFO("Shutdown did not run OnDestroy exactly once");
                REQUIRE(CountMessages(headless, LogLevel::Info, "Creator.OnDestroy") == 1);
            }
        }
    }

    void ASystemsCallsAfterShutdownDoNothing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        scripting.Shutdown();
        headless.Run(2); // The scheduled phase systems find nothing to run.
        {
            INFO("A phase ran after Shutdown");
            REQUIRE(ScriptingProbe::PhaseCalls(scripting) == 0);
        }
    }

} // namespace

TEST_CASE("systems run in phase and order, with changes applied after each", "[scripting]")
{
    SystemsRunInPhaseAndOrderAndChangesApplyAfterEachOne();
}

TEST_CASE("OnCreate runs after load and OnDestroy before release", "[scripting]")
{
    OnCreateRunsAfterLoadAndOnDestroyBeforeRelease();
}

TEST_CASE("a phase does nothing after Shutdown", "[scripting]")
{
    ASystemsCallsAfterShutdownDoNothing();
}
