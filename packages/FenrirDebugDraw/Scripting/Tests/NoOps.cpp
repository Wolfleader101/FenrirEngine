// The no-op rules (spec: "Scripting: the extensions", no-op rules; seam 7: a missing package no-oping), with no .NET:
// each native function is called from C++ through NativeCallHarness with a DebugDrawScripting state the test makes.
// With no DebugDraw in the active scene or a relativeTo entity that is not alive, a call draws nothing, IsGroupShown is
// false, and a debug build warns once per native and reason, however often it is called.

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "DebugDrawScriptingState.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/EntityValue.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "Generated/FenrirDebugDrawNatives.hpp"
#include "ScriptOptions.hpp"

namespace
{
    using namespace Fenrir;

    // A release build warns about nothing (WarnOnceRecord, ADR-0043); a debug build once per place and message.
#ifdef NDEBUG
    constexpr std::size_t once = 0;
#else
    constexpr std::size_t once = 1;
#endif

    constexpr const char* NoDebugDraw =
        "the active scene has no DebugDraw (FenrirDebugDraw is not installed), so it draws nothing";
    constexpr const char* NotAlive = "the entity is not alive, so it does nothing";

    // How many warnings the app logged that read `text`.
    std::size_t Warnings(const Testing::NativeCallHarness& harness, const std::string& text)
    {
        const auto messages = harness.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
            }));
    }

    // The warning the extension's record logs for `message` in the native `native`.
    std::string Logged(const std::string& native, const std::string& message)
    {
        return "[debug draw scripting] " + native + ": " + message + " (warned once)";
    }

    const Math::Vec3 origin{0.0f, 0.0f, 0.0f};
    const Math::Vec3 unit{1.0f, 1.0f, 1.0f};
    const Math::Vec2 corner{4.0f, 4.0f};
    const Colour white{1.0f, 1.0f, 1.0f, 1.0f};
} // namespace

TEST_CASE("Items drawn relative to an entity that is not alive draw nothing and say so once per native",
          "[debugdraw][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    const DebugDrawing drawing = DebugDrawing::Install(harness.GetApp());
    DebugDrawScripting debugDraw;
    EntityList& entities = harness.GetApp().GetActiveScene().GetEntityList();
    const std::uint32_t dead = entities.CreateEntity().GetId();
    entities.DestroyEntity(dead);
    ScriptOptions::ShapeOptions shape;
    shape.relativeTo = EntityValue(dead);
    ScriptOptions::TextOptions text;
    text.relativeTo = EntityValue(dead);

    for (int call = 0; call < 2; ++call)
    {
        DebugDrawNatives::Line(harness.Context("DebugDrawNative.Line"), debugDraw, &origin, &unit, &white, &shape, "");
        DebugDrawNatives::WorldText(harness.Context("DebugDrawNative.WorldText"), debugDraw, &origin, "hello", &white,
                                    &text, "");
    }
    {
        const DebugDraw* draw = harness.GetApp().GetActiveScene().GetResource<DebugDraw>();
        INFO("Debug drawing was installed, so the active scene should have a DebugDraw");
        REQUIRE(draw != nullptr);
        INFO("Something was drawn relative to a dead entity");
        REQUIRE(draw->Tessellate().Empty());
    }
    INFO("The line and the world text should each warn once in a debug build, and never in a release build");
    REQUIRE(Warnings(harness, Logged("DebugDrawNative.Line", NotAlive)) == once);
    REQUIRE(Warnings(harness, Logged("DebugDrawNative.WorldText", NotAlive)) == once);
}

TEST_CASE("With no debug drawing installed, each native draws nothing, sees no group shown and says so once",
          "[debugdraw][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    DebugDrawScripting debugDraw;
    const ScriptOptions::ShapeOptions shape;
    const ScriptOptions::TextOptions text;

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": no group should be shown");
        DebugDrawNatives::Line(harness.Context("DebugDrawNative.Line"), debugDraw, &origin, &unit, &white, &shape, "");
        DebugDrawNatives::ScreenText(harness.Context("DebugDrawNative.ScreenText"), debugDraw, &corner, "hello", &white,
                                     &text, "");
        DebugDrawNatives::Message(harness.Context("DebugDrawNative.Message"), debugDraw, "hello", &white, &text, "");
        DebugDrawNatives::KeyedMessage(harness.Context("DebugDrawNative.KeyedMessage"), debugDraw, 7, "hello", &white,
                                       &text, "");
        DebugDrawNatives::WorldText(harness.Context("DebugDrawNative.WorldText"), debugDraw, &origin, "hello", &white,
                                    &text, "");
        DebugDrawNatives::HideGroup(harness.Context("DebugDrawNative.HideGroup"), debugDraw, "group");
        DebugDrawNatives::Clear(harness.Context("DebugDrawNative.Clear"), debugDraw);
        DebugDrawNatives::ClearGroup(harness.Context("DebugDrawNative.ClearGroup"), debugDraw, "group");
        REQUIRE_FALSE(DebugDrawNatives::IsGroupShown(harness.Context("DebugDrawNative.IsGroupShown"), debugDraw, ""));
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
        REQUIRE(Warnings(harness, Logged(native, NoDebugDraw)) == once);
    }
    {
        INFO("A relativeTo entity should not be checked when there is nothing to draw with");
        REQUIRE(Warnings(harness, NotAlive) == 0);
    }
}
