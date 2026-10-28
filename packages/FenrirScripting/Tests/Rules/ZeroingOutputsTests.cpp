// The registration's zeroing of outputs (ADR-0045), with no .NET: ZeroingOutputs<&Native, indices...>::Call is what the
// generated registration gives NativeFunction::Make, so it must zero exactly the named outputs, forward everything else
// as it is, and sit inside the not-running check.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include "FenrirScripting/NativeFunctionMake.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/ZeroingOutputs.hpp"

#include <cstdint>
#include <string>

namespace
{
    using namespace Fenrir;

    struct Hit
    {
        float distance = 0.0F;
        std::int32_t entity = 0;

        [[nodiscard]] friend bool operator==(Hit, Hit) = default;
    };

    // What the native saw when it was called: each argument as it arrived.
    struct Seen
    {
        int calls = 0;
        Hit hit{};
        const Hit* hitAddress = nullptr;
        Hit* hits = nullptr;
        std::int32_t capacity = 0;
        float total = 0.0F;
        std::int32_t code = 0;
    };

    // A native with a single output (`hit`), an output span (`hits` and `capacity`) and an inOut pointer (`total`):
    // the registration zeroes the first and must leave the other two as C# passed them.
    std::int32_t Probe(const CallContext&, Seen& seen, Hit* hit, Hit* hits, std::int32_t capacity, float* total,
                       std::int32_t code)
    {
        ++seen.calls;
        seen.hit = *hit;
        seen.hitAddress = hit;
        seen.hits = hits;
        seen.capacity = capacity;
        seen.total = *total;
        seen.code = code;
        return code * 2;
    }

    using Zeroing = ZeroingOutputs<&Probe, 2>;
} // namespace

TEST_CASE("ZeroingOutputs zeroes the named outputs and forwards every argument and the result", "[rules][natives]")
{
    Testing::NativeCallHarness harness;
    const CallContext context = harness.Context("Probe.Native");
    Seen seen;
    Hit hit{3.5F, 7};
    Hit hits[2] = {{1.0F, 1}, {2.0F, 2}};
    float total = 9.0F;

    const std::int32_t result = Zeroing::Call(context, seen, &hit, hits, 2, &total, 21);
    {
        INFO("The native's result did not come back through the zeroing call");
        REQUIRE(result == 42);
    }
    {
        INFO("The native was not called exactly once");
        REQUIRE(seen.calls == 1);
    }
    {
        INFO("The output was not zeroed before the native ran");
        REQUIRE(seen.hit == Hit{});
        REQUIRE(hit == Hit{});
    }
    {
        INFO("The arguments did not reach the native as they were passed");
        REQUIRE(seen.hitAddress == &hit);
        REQUIRE(seen.hits == hits);
        REQUIRE(seen.capacity == 2);
        REQUIRE(seen.total == 9.0F);
        REQUIRE(seen.code == 21);
    }
}

TEST_CASE("ZeroingOutputs leaves an output span and an inOut pointer untouched", "[rules][natives]")
{
    Testing::NativeCallHarness harness;
    const CallContext context = harness.Context("Probe.Native");
    Seen seen;
    Hit hit{3.5F, 7};
    Hit hits[2] = {{1.0F, 1}, {2.0F, 2}};
    float total = 9.0F;

    (void)Zeroing::Call(context, seen, &hit, hits, 2, &total, 1);
    {
        INFO("The output span, governed by its returned count, was written to");
        REQUIRE(hits[0] == Hit{1.0F, 1});
        REQUIRE(hits[1] == Hit{2.0F, 2});
    }
    {
        INFO("The inOut pointer, which is C#'s value, was zeroed");
        REQUIRE(total == 9.0F);
    }
}

TEST_CASE("ZeroingOutputs is a native function NativeFunction::Make accepts, zeroing inside the not-running check",
          "[rules][natives]")
{
    const NativeFunction made = NativeFunction::Make<&Zeroing::Call>("Fenrir.Rules.Probe.Native", -1);
    {
        INFO("The zeroing call was not registered under the native's name");
        REQUIRE(made.Name() == "Fenrir.Rules.Probe.Native");
    }
    // No Scripting runs in this executable, so the thunk's call is the not-running path: the native must not run and
    // its output must stay as C# passed it.
    using Thunk = Detail::NativeFunctionTraits<&Zeroing::Call>;
    Hit hit{3.5F, 7};
    Hit hits[1] = {{1.0F, 1}};
    float total = 9.0F;
    Seen* const noState = nullptr;
    Thunk::state = noState;
    const std::int32_t result = Thunk::Call(&hit, hits, 1, &total, 21);
    {
        INFO("A call while no Scripting runs did not return the not-running value");
        REQUIRE(result == Thunk::notRunning);
    }
    {
        INFO("A call while no Scripting runs touched the output");
        REQUIRE(hit == Hit{3.5F, 7});
    }
}
