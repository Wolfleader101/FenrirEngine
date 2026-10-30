// Extension stages through the real runtime: the test extension hosts a stage of its own, its bench, and the extension
// script's stage systems join it. Groups run by order among the bench's C++ items, C++ first at an equal order; a stage
// no extension declares fails the load; what stage systems record plays back when the bench returns; a throw drops the
// thrower's changes and disables it until a reload, which replaces the groups; and a stage that never runs is no error.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;
    using Trace = std::vector<std::string>;

    constexpr const char* StageCallsType = "Fenrir.TestExtensionScript.StageCalls";

    // A C++ item of the bench at `order`, tracing "C++ <order>".
    TestExtension::BenchItem Item(TestExtension::Tally& tally, std::int32_t order)
    {
        return {order, false, [&tally, order] { tally.trace.push_back("C++ " + std::to_string(order)); }};
    }

    // The entity a "C# 30 spawned <id>" line of the trace names.
    std::uint32_t SpawnedIn(const std::string& line)
    {
        return static_cast<std::uint32_t>(std::stoul(line.substr(line.rfind(' ') + 1)));
    }

    bool HasCharge(App& app, std::uint32_t id)
    {
        const Entity entity = app.GetActiveScene().GetEntityList().GetEntity(id);
        return entity.IsValid() && entity.HasComponent<TestExtension::Charge>();
    }

    std::size_t ScriptItems(const TestExtension::Tally& tally)
    {
        return static_cast<std::size_t>(std::count_if(
            tally.bench.begin(), tally.bench.end(), [](const TestExtension::BenchItem& item) { return item.script; }));
    }

    void GroupsRunByOrderAmongTheExtensionsWork()
    {
        HeadlessApp headless;
        auto tally = std::make_shared<TestExtension::Tally>();
        tally->bench = {Item(*tally, 10), Item(*tally, 20)};
        // The last C++ item looks for the Charge the spawner gave its entity: not there until the bench returns.
        bool sawCharge = true;
        tally->bench.push_back({40, false, [&] {
                                    const auto spawned = std::find_if(
                                        tally->trace.begin(), tally->trace.end(),
                                        [](const std::string& line) { return line.starts_with("C# 30 spawned "); });
                                    sawCharge =
                                        spawned != tally->trace.end() && HasCharge(headless.Get(), SpawnedIn(*spawned));
                                    tally->trace.push_back("C++ 40");
                                }});
        // The bench is the extension's work, which the app runs once a frame as a package runs its own.
        headless.Get().AddSequentialSystems(SchedulePriority::Update,
                                            {[tally](App&) { TestExtension::RunBench(*tally); }});
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(tally)}));
        {
            INFO("The extension script did not load: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("Each (stage, order) group should be one item: 15, 20 and 30");
            REQUIRE(ScriptItems(*tally) == 3);
        }

        headless.Run(1);

        {
            INFO("Nothing should have been logged as an error: " + Errors(headless));
            REQUIRE(Errors(headless).empty());
        }
        {
            INFO("The trace should have 8 entries, one per item and system");
            REQUIRE(tally->trace.size() == 8);
        }
        const std::string spawned = tally->trace[5];
        const Trace expected{"C++ 10", "C# 15", "C++ 20", "C# 20 A", "C# 20 B", spawned, "C# 30 thrower", "C++ 40"};
        {
            INFO("C# groups should run by order among the C++ items, C++ first at 20, a group's systems by name");
            REQUIRE(tally->trace == expected);
        }
        {
            INFO("The spawner's line should be at 30, before the thrower's");
            REQUIRE(spawned.starts_with("C# 30 spawned "));
        }
        {
            INFO("The spawner's Charge should not be there while the bench runs");
            REQUIRE(!sawCharge);
        }
        {
            INFO("The spawner's Charge should be there once the bench returns");
            REQUIRE(HasCharge(headless.Get(), SpawnedIn(spawned)));
        }
        {
            INFO("Releasing the script should remove its groups from the bench");
            REQUIRE((ScriptItems(*tally) == 0 && tally->bench.size() == 3));
        }
    }

    void AThrowDisablesTheSystemUntilAReloadWhichReplacesTheGroups()
    {
        HeadlessApp headless;
        auto tally = std::make_shared<TestExtension::Tally>();
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(tally)}));
        {
            INFO("The extension script did not load: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        CallIn<void>(StageCallsType, scripting, "ArmThrower");

        TestExtension::RunBench(*tally);
        const auto lineFor = [&](const std::string& start) {
            const auto found = std::find_if(tally->trace.begin(), tally->trace.end(),
                                            [&](const std::string& line) { return line.starts_with(start); });
            REQUIRE(found != tally->trace.end());
            return SpawnedIn(*found);
        };
        const std::uint32_t kept = lineFor("C# 30 spawned ");
        const std::uint32_t dropped = lineFor("C# 30 dropped ");
        {
            INFO("The thrower should have been logged once, with its name and its exception");
            REQUIRE(CountMessages(headless, LogLevel::Error, "BenchThrower") == 1);
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "bench kaboom"));
        }
        {
            INFO("The spawner's changes, recorded before the throw, should have played back");
            REQUIRE(HasCharge(headless.Get(), kept));
        }
        {
            INFO("The thrower's entity should be gone: its changes were dropped, the spawner's kept");
            auto& entities = headless.Get().GetActiveScene().GetEntityList();
            REQUIRE((dropped != kept && !entities.HasEntity(dropped)));
        }

        tally->trace.clear();
        TestExtension::RunBench(*tally);
        {
            INFO("The thrower should be disabled, and the others still run");
            REQUIRE(std::find(tally->trace.begin(), tally->trace.end(), "C# 30 thrower") == tally->trace.end());
            REQUIRE(std::find(tally->trace.begin(), tally->trace.end(), "C# 15") != tally->trace.end());
        }

        const auto reloaded = scripting.Reload();
        {
            INFO("The reload should succeed: " + reloaded.diagnostic);
            REQUIRE(reloaded.Succeeded());
        }
        {
            INFO("The reload should have replaced the groups, not added to them");
            REQUIRE(ScriptItems(*tally) == 3);
        }
        tally->trace.clear();
        TestExtension::RunBench(*tally);
        {
            INFO("After the reload the thrower should run again, unarmed");
            REQUIRE(std::find(tally->trace.begin(), tally->trace.end(), "C# 30 thrower") != tally->trace.end());
            REQUIRE(CountMessages(headless, LogLevel::Error, "BenchThrower") == 1);
        }
        {
            INFO("Each group should have run once per bench run");
            REQUIRE(std::count(tally->trace.begin(), tally->trace.end(), "C# 15") == 1);
        }
        scripting.Shutdown();
    }

    void AStageNoExtensionDeclaresFailsTheLoad()
    {
        HeadlessApp headless;
        auto tally = std::make_shared<TestExtension::Tally>();
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(tally, false)}));
        {
            INFO("The script should not have loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The error should name the system and the stage: " + Errors(headless));
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "Fenrir.TestExtensionScript.BenchMiddle"));
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "joins the stage 'Fenrir.TestExtension.Bench'"));
        }
        {
            INFO("No system of the script should have run");
            REQUIRE(tally->trace.empty());
        }
    }

    void AStageThatNeverRunsIsNoError()
    {
        HeadlessApp headless;
        auto tally = std::make_shared<TestExtension::Tally>();
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(tally)}));
        REQUIRE(scripting.HasScripts());
        // Nothing runs the bench: its package might not be installed, or its scene inactive.
        headless.Run(3);
        {
            INFO("Nothing should have been logged as an error, nor a warning of a stage: " + Errors(headless));
            REQUIRE(Errors(headless).empty());
            REQUIRE(CountMessages(headless, LogLevel::Warn, "stage") == 0);
        }
        {
            INFO("No stage system should have run");
            REQUIRE(tally->trace.empty());
        }
    }
} // namespace

TEST_CASE("stage groups run by order among the extension's C++ work, and play back when it returns",
          "[scripting][stages]")
{
    GroupsRunByOrderAmongTheExtensionsWork();
}

TEST_CASE("a throwing stage system loses its changes and is disabled until a reload replaces the groups",
          "[scripting][stages]")
{
    AThrowDisablesTheSystemUntilAReloadWhichReplacesTheGroups();
}

TEST_CASE("a stage system naming a stage no extension declares fails the load", "[scripting][stages]")
{
    AStageNoExtensionDeclaresFailsTheLoad();
}

TEST_CASE("a stage that never runs is not an error", "[scripting][stages]")
{
    AStageThatNeverRunsIsNoError();
}
