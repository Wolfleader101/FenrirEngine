// The no-op rules from C# (spec: "Scripting: the extensions", no-op rules; seam 7: a missing package no-oping): with no
// DebugDraw in the active scene or a relativeTo entity that is not alive, a call draws nothing, IsGroupShown is false,
// nothing throws, and a debug build warns once per native call site and reason, however often C# calls.
//
// The warnings are counted once per process, so these cases run in a process of their own, in declaration order
// (CTest's FenrirDebugDrawScripting-NoOps runs them with --order decl).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "DebugDrawScriptingApp.hpp"

using namespace Fenrir;
using namespace DebugDrawScriptingTests;

namespace
{
    std::size_t Warnings(const DebugDrawScriptingApp& test, const std::string& text)
    {
        return LoggedWith(test.headless, Testing::LogLevel::Warn, text);
    }
} // namespace

TEST_CASE("Items drawn from C# relative to an entity that is not alive draw nothing and say so once",
          "[debugdraw][scripting][noops]")
{
    DebugDrawScriptingApp test;
    const std::uint32_t dead = test.Entities().CreateEntity().GetId();
    test.Entities().DestroyEntity(dead);

    for (int call = 0; call < 2; ++call)
    {
        test.Call<void>(ItemCallsType, "RelativeTo", dead);
    }
    {
        INFO("Nothing should have been drawn");
        REQUIRE(test.Draw().Tessellate().Empty());
    }
    INFO("The line and the world text should each warn once in a debug build, and never in a release build");
    REQUIRE(Warnings(test, "DebugDraw.Line: the relativeTo entity is not alive") == warningsPerCallSite);
    REQUIRE(Warnings(test, "DebugDraw.WorldText: the relativeTo entity is not alive") == warningsPerCallSite);
}

TEST_CASE("With no debug drawing installed, C# draws nothing, sees no group shown and says so once",
          "[debugdraw][scripting][noops]")
{
    DebugDrawScriptingApp test(false);
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": every call should return without throwing, and no group should be shown");
        test.Call<void>(ShapeCallsType, "Draw", 0, 1);
        test.Call<void>(TextCallsType, "Draw");
        test.Call<void>(ItemCallsType, "Hide", 1);
        test.Call<void>(ItemCallsType, "ClearAll");
        REQUIRE_FALSE(test.Call<bool>(ItemCallsType, "IsShown", 0));
    }
    {
        INFO("The active scene should still have no DebugDraw");
        REQUIRE(test.Get().GetActiveScene().GetResource<DebugDraw>() == nullptr);
    }
    INFO("Each function should warn once in a debug build, and never in a release build, that there is no DebugDraw");
    for (const char* function : {"DebugDraw.Line", "DebugDraw.ScreenText", "DebugDraw.Message", "DebugDraw.WorldText",
                                 "DebugDraw.HideGroup", "DebugDraw.Clear", "DebugDraw.IsGroupShown"})
    {
        INFO(function);
        // Message has two call sites in the native code, the plain and the keyed message.
        const std::size_t sites = std::string(function) == "DebugDraw.Message" ? 2 : 1;
        REQUIRE(Warnings(test, std::string(function) + ": the active scene has no DebugDraw") ==
                sites * warningsPerCallSite);
    }
}
