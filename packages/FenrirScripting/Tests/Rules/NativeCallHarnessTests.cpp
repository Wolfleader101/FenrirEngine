// The native call harness runs an extension as shipped, with no .NET: given the test extension, it registers its
// natives as Scripting::Install does and calls one by its registered name through the registration's own thunk, so
// the context, the state, the zeroing of outputs and the warning each come from the registration, not the test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "NativeCallHarness.hpp"
#include "TestExtension.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/NativeGuard.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace Fenrir;
    using Catch::Matchers::ContainsSubstring;

    // The test extension with its state, as a test would run a package's: one stateless native (Add), one stateful
    // (Tally, Nudge) and one with an output (Weigh).
    std::vector<ScriptingExtension> Extensions(std::shared_ptr<TestExtension::Tally> tally)
    {
        std::vector<ScriptingExtension> extensions;
        extensions.push_back(Testing::ScriptFixture::TheTestExtension(std::move(tally)));
        return extensions;
    }
} // namespace

TEST_CASE("NativeCallHarness calls a stateless and a stateful native by its registered name through its thunk",
          "[rules][harness]")
{
    auto tally = std::make_shared<TestExtension::Tally>();
    Testing::NativeCallHarness harness(Extensions(tally));
    {
        INFO("A stateless native called by name did not return its result");
        REQUIRE(harness.Call<std::int32_t>("Gadgets.Add", std::int32_t{2}, std::int32_t{3}) == 5);
    }
    {
        INFO("A stateful native called by name did not get the extension's state");
        REQUIRE(harness.Call<std::int32_t>("Gadgets.Tally", std::int32_t{4}) == 4);
        REQUIRE(harness.Call<std::int32_t>("Gadgets.Tally", std::int32_t{6}) == 10);
        REQUIRE(tally->total == 10);
    }
}

TEST_CASE("NativeCallHarness refuses a wrong result type, a wrong argument type and an unregistered name, naming the "
          "native",
          "[rules][harness]")
{
    Testing::NativeCallHarness harness(Extensions(std::make_shared<TestExtension::Tally>()));
    {
        INFO("A call with the wrong result type did not throw naming the native");
        REQUIRE_THROWS_WITH(harness.Call<float>("Gadgets.Add", std::int32_t{2}, std::int32_t{3}),
                            ContainsSubstring("Gadgets.Add"));
    }
    {
        INFO("A call with a wrong argument type did not throw naming the native");
        REQUIRE_THROWS_WITH(harness.Call<std::int32_t>("Gadgets.Add", 2.0F, std::int32_t{3}),
                            ContainsSubstring("Gadgets.Add"));
    }
    {
        INFO("A call of a name no extension registered did not throw naming it");
        REQUIRE_THROWS_WITH(harness.Call<void>("Gadgets.Vanish"), ContainsSubstring("Gadgets.Vanish"));
    }
    {
        INFO("A stateful native given an extension carrying no state was registered");
        REQUIRE_THROWS_WITH(Testing::NativeCallHarness(Extensions(nullptr)), ContainsSubstring("Gadgets.Tally"));
    }
}

TEST_CASE("NativeCallHarness is the active Scripting while it lives, so the thunk builds the call context from its app",
          "[rules][harness]")
{
    auto tally = std::make_shared<TestExtension::Tally>();
    Testing::NativeCallHarness harness(Extensions(tally));
    EntityList& entities = harness.GetApp().GetActiveScene().GetEntityList();
    const Entity live = entities.CreateEntity();
    live.GetComponent<Transform>().pos.y = 1.5F;
    const Entity dead = entities.HandleOf(entities.CreateEntity().GetId());
    entities.DestroyEntity(dead.GetId());
    {
        INFO("A second harness while one lives did not throw");
        REQUIRE_THROWS_AS(Testing::NativeCallHarness(), std::runtime_error);
    }
    {
        INFO("A native reading the active scene through its context did not see the harness's app");
        REQUIRE(harness.Call<float>("Gadgets.HeightOf", &live) == 1.5F);
    }
    for (int call = 0; call < 2; ++call)
    {
        harness.Call<void>("Gadgets.Nudge", &dead);
    }
    {
        INFO("A native warning through the guard under its registered name was not counted once");
        REQUIRE(harness.WarnedOnce("Gadgets.Nudge", NativeGuard::NotAlive));
    }
    {
        INFO("Nudging a live entity twice did not raise it twice, or warned");
        harness.Call<void>("Gadgets.Nudge", &live);
        harness.Call<void>("Gadgets.Nudge", &live);
        REQUIRE(live.GetComponent<Transform>().pos.y == 3.5F);
        REQUIRE(harness.Warnings("Gadgets.Nudge", NativeGuard::NotAlive) == harness.once);
    }
}

TEST_CASE("NativeCallHarness calls a native with an output through its registration, which zeroes the output before "
          "an early return",
          "[rules][harness]")
{
    Testing::NativeCallHarness harness(Extensions(std::make_shared<TestExtension::Tally>()));
    EntityList& entities = harness.GetApp().GetActiveScene().GetEntityList();
    const Entity live = entities.CreateEntity();
    live.GetComponent<Transform>().pos.y = 2.5F;
    const Entity dead = entities.HandleOf(entities.CreateEntity().GetId());
    entities.DestroyEntity(dead.GetId());
    TestExtension::Reading reading{3, 9.0F};
    {
        INFO("A live entity did not give one reading of its height");
        REQUIRE(harness.Call<std::int32_t>("Gadgets.Weigh", &live, &reading) == 1);
        REQUIRE((reading.Count == 1 && reading.Weight == 2.5F));
    }
    reading = TestExtension::Reading{3, 9.0F};
    {
        INFO("A dead entity did not return early with the output zeroed by the registration");
        REQUIRE(harness.Call<std::int32_t>("Gadgets.Weigh", &dead, &reading) == 0);
        REQUIRE((reading.Count == 0 && reading.Weight == 0.0F));
    }
}
