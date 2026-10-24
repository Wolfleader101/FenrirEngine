// Durations, groups and relativeTo from C# (spec seam 7: debug draw from C#): C# items follow C++'s lifetime rules for
// the phase they are drawn in, C# shows, hides and clears groups, and an item relative to an entity follows it.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "DebugDrawScriptingApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"

using namespace Fenrir;
using namespace DebugDrawScriptingTests;
using Math::Vec3;

namespace
{
    bool Near(const Vec3& a, const Vec3& b)
    {
        return std::abs(a.x - b.x) <= 1e-4f && std::abs(a.y - b.y) <= 1e-4f && std::abs(a.z - b.z) <= 1e-4f;
    }

    // The fixture's group numbers.
    constexpr int defaultGroup = 0;
    constexpr int probe = 1;
    constexpr int other = 2;
} // namespace

TEST_CASE("A line drawn from a C# Tick system lasts until the next tick", "[debugdraw][scripting]")
{
    DebugDrawScriptingApp test;
    test.Call<void>(TickDrawerType, "Arm");

    {
        INFO("The frame whose tick drew the line should show it");
        REQUIRE(LineCount(test.Frame(oneTick)) == 1);
        REQUIRE(test.Call<int>(TickDrawerType, "Ticks") == 1);
    }
    {
        INFO("A frame with no tick should still show it");
        REQUIRE(LineCount(test.Frame(noTick)) == 1);
        REQUIRE(test.Call<int>(TickDrawerType, "Ticks") == 1);
    }
    INFO("After the next tick, which drew nothing, it should be gone");
    REQUIRE(LineCount(test.Frame(oneTick)) == 0);
    REQUIRE(test.Call<int>(TickDrawerType, "Ticks") == 2);
}

TEST_CASE("A line drawn from C# for some seconds lasts that long, and one drawn forever until cleared",
          "[debugdraw][scripting]")
{
    DebugDrawScriptingApp test;
    test.Call<void>(ItemCallsType, "LineFor", 0.1f);
    test.Call<void>(ItemCallsType, "LineForever");

    {
        INFO("Both lines should show in the first frames");
        REQUIRE(LineCount(test.Frame(0.05)) == 2);
    }
    test.Frame(0.05);
    test.Frame(0.05);
    {
        INFO("After more than 0.1 s only the forever line should be left");
        REQUIRE(LineCount(test.Frame(0.05)) == 1);
    }
    test.Call<void>(ItemCallsType, "ClearAll");
    INFO("Clear from C# should drop the forever line");
    REQUIRE(LineCount(test.Frame()) == 0);
}

TEST_CASE("C# shows, hides and clears groups", "[debugdraw][scripting]")
{
    DebugDrawScriptingApp test;
    // No frame runs in this case, so the duration-0 lines drawn outside a system stay for all of it.
    test.Call<void>(ItemCallsType, "LineForever");
    test.Call<void>(ItemCallsType, "LineIn", probe);
    test.Call<void>(ItemCallsType, "LineIn", other);
    {
        INFO("Every group should be shown at first, so all three lines are drawn");
        REQUIRE(test.Call<bool>(ItemCallsType, "IsShown", probe));
        REQUIRE(LineCount(test.Draw().Tessellate()) == 3);
    }

    test.Call<void>(ItemCallsType, "Hide", probe);
    {
        INFO("Hidden from C#, the Probe group's line should not be drawn, and both C# and C++ should say so");
        REQUIRE_FALSE(test.Call<bool>(ItemCallsType, "IsShown", probe));
        REQUIRE_FALSE(test.Draw().IsGroupShown("Probe"));
        REQUIRE(test.Draw().IsGroupShown("Other"));
        REQUIRE(LineCount(test.Draw().Tessellate()) == 2);
    }

    test.Call<void>(ItemCallsType, "Show", probe);
    {
        INFO("Shown again from C#, its line should be drawn again, having been kept while hidden");
        REQUIRE(test.Call<bool>(ItemCallsType, "IsShown", probe));
        REQUIRE(LineCount(test.Draw().Tessellate()) == 3);
    }

    test.Call<void>(ItemCallsType, "Clear", other);
    {
        INFO("Clearing the Other group from C# should drop only its line");
        REQUIRE(LineCount(test.Draw().Tessellate()) == 2);
    }

    test.Call<void>(ItemCallsType, "Clear", defaultGroup);
    INFO("Clearing the Default group from C# should drop the forever line, leaving Probe's");
    REQUIRE(LineCount(test.Draw().Tessellate()) == 1);
}

TEST_CASE("Items drawn from C# relative to an entity follow it and go with it", "[debugdraw][scripting]")
{
    DebugDrawScriptingApp test;
    Entity mover = test.Entities().CreateEntity();
    mover.AddComponent<Transform>().pos = Vec3(10.0f, 0.0f, 0.0f);
    test.Call<void>(ItemCallsType, "RelativeTo", mover.GetId());

    const DebugBatches first = test.Frame();
    {
        INFO("The line should be placed by the entity: from (10, 0, 0) to (11, 0, 0), and the text at (10, 1, 0)");
        REQUIRE(LineCount(first) == 1);
        REQUIRE(Near(first.lines[0].vertices[0].position, Vec3(10.0f, 0.0f, 0.0f)));
        REQUIRE(Near(first.lines[0].vertices[1].position, Vec3(11.0f, 0.0f, 0.0f)));
        REQUIRE(first.texts.size() == 1);
        REQUIRE(Near(first.texts[0].position, Vec3(10.0f, 1.0f, 0.0f)));
    }

    mover.GetComponent<Transform>().pos = Vec3(0.0f, 5.0f, 0.0f);
    const DebugBatches moved = test.Frame();
    {
        INFO("When the entity moves the line should follow it: from (0, 5, 0) to (1, 5, 0)");
        REQUIRE(LineCount(moved) == 1);
        REQUIRE(Near(moved.lines[0].vertices[0].position, Vec3(0.0f, 5.0f, 0.0f)));
        REQUIRE(Near(moved.lines[0].vertices[1].position, Vec3(1.0f, 5.0f, 0.0f)));
    }

    test.Entities().DestroyEntity(mover);
    INFO("Once the entity is destroyed its items should be gone, though drawn forever");
    REQUIRE(test.Frame().Empty());
}
