// The no-op rules (spec: "Scripting: the extensions", no-op rules; seam 7: a missing package no-oping), with no .NET:
// through NativeCallHarness, which runs the debug draw extension as shipped (DebugDrawing::ScriptingExtension()), each
// native function is called from C++ by its registered name through the registration's own thunk. With no DebugDraw in
// the active scene or a relativeTo entity that is not alive, a call draws nothing, IsGroupShown is false, and a debug
// build warns once per native and reason, however often it is called, in the words DebugDrawScripting and the native
// guard name.

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "DebugDrawScriptingState.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirDebugDrawScripting/DebugDrawScripting.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "ScriptOptions.hpp"

namespace
{
    using namespace Fenrir;
    using namespace std::string_view_literals;

    const Math::Vec3 origin{0.0f, 0.0f, 0.0f};
    const Math::Vec3 unit{1.0f, 1.0f, 1.0f};
    const Math::Vec2 corner{4.0f, 4.0f};
    const Colour white{1.0f, 1.0f, 1.0f, 1.0f};

    // The options C# passes for an item drawn relative to `entity`, with everything else at its default.
    ScriptOptions::ShapeOptions ShapeRelativeTo(const Entity& entity)
    {
        ScriptOptions::ShapeOptions options;
        options.relativeTo = entity;
        return options;
    }
    ScriptOptions::TextOptions TextRelativeTo(const Entity& entity)
    {
        ScriptOptions::TextOptions options;
        options.relativeTo = entity;
        return options;
    }
} // namespace

TEST_CASE("Items drawn relative to an entity that is not alive draw nothing and say so once per native",
          "[debugdraw][scripting][noops]")
{
    Testing::NativeCallHarness harness({DebugDrawing::ScriptingExtension()});
    const DebugDrawing drawing = DebugDrawing::Install(harness.GetApp());
    EntityList& entities = harness.GetApp().GetActiveScene().GetEntityList();
    const std::uint32_t dead = entities.CreateEntity().GetId();
    entities.DestroyEntity(dead);
    const ScriptOptions::ShapeOptions shape = ShapeRelativeTo(Entity(dead, &entities));
    const ScriptOptions::TextOptions text = TextRelativeTo(Entity(dead, &entities));

    for (int call = 0; call < 2; ++call)
    {
        harness.Call<void>("DebugDrawNative.Line", &origin, &unit, &white, &shape, ""sv);
        harness.Call<void>("DebugDrawNative.WorldText", &origin, "hello"sv, &white, &text, ""sv);
    }
    {
        const DebugDraw* draw = harness.GetApp().GetActiveScene().GetResource<DebugDraw>();
        INFO("Debug drawing was installed, so the active scene should have a DebugDraw");
        REQUIRE(draw != nullptr);
        INFO("Something was drawn relative to a dead entity");
        REQUIRE(draw->Tessellate().Empty());
    }
    INFO("The line and the world text should each warn once in a debug build, and never in a release build");
    REQUIRE(harness.WarnedOnce("DebugDrawNative.Line", NativeGuard::NotAlive));
    REQUIRE(harness.WarnedOnce("DebugDrawNative.WorldText", NativeGuard::NotAlive));
}

TEST_CASE("With no debug drawing installed, each native draws nothing, sees no group shown and says so once",
          "[debugdraw][scripting][noops]")
{
    Testing::NativeCallHarness harness({DebugDrawing::ScriptingExtension()});
    const ScriptOptions::ShapeOptions shape;
    const ScriptOptions::TextOptions text;

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": no group should be shown");
        harness.Call<void>("DebugDrawNative.Line", &origin, &unit, &white, &shape, ""sv);
        harness.Call<void>("DebugDrawNative.ScreenText", &corner, "hello"sv, &white, &text, ""sv);
        harness.Call<void>("DebugDrawNative.Message", "hello"sv, &white, &text, ""sv);
        harness.Call<void>("DebugDrawNative.KeyedMessage", std::int64_t{7}, "hello"sv, &white, &text, ""sv);
        harness.Call<void>("DebugDrawNative.WorldText", &origin, "hello"sv, &white, &text, ""sv);
        harness.Call<void>("DebugDrawNative.HideGroup", "group"sv);
        harness.Call<void>("DebugDrawNative.Clear");
        harness.Call<void>("DebugDrawNative.ClearGroup", "group"sv);
        REQUIRE_FALSE(harness.Call<bool>("DebugDrawNative.IsGroupShown", ""sv));
    }
    {
        INFO("The active scene should still have no DebugDraw");
        REQUIRE(harness.GetApp().GetActiveScene().GetResource<DebugDraw>() == nullptr);
    }
    INFO("Each native should warn once in a debug build, and never in a release build, that there is no DebugDraw");
    for (const char* native : {"DebugDrawNative.Line", "DebugDrawNative.ScreenText", "DebugDrawNative.Message",
                               "DebugDrawNative.KeyedMessage", "DebugDrawNative.WorldText", "DebugDrawNative.HideGroup",
                               "DebugDrawNative.Clear", "DebugDrawNative.ClearGroup", "DebugDrawNative.IsGroupShown"})
    {
        INFO(native);
        REQUIRE(harness.WarnedOnce(native, DebugDrawScripting::NoDebugDraw));
    }
    {
        INFO("A relativeTo entity should not be checked when there is nothing to draw with");
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, std::string(NativeGuard::NotAlive)));
    }
}
