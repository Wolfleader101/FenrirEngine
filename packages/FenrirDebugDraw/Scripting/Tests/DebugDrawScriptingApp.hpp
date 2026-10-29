#pragma once

// The debug draw scripting tests' part of FenrirScripting's ExtensionTestApp (spec seam 7: debug draw from C#): the
// fixture CTest names in FENRIR_TEST_DEBUGDRAW_FIXTURE, debug drawing installed, and the debug draw extension. C++
// calls the fixture's static methods through Iris and reads what C# drew through the active scene's DebugDraw: its
// tessellated batches (seam 6).

#include "ExtensionTestApp.hpp"

#include <cstddef>

#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirDebugDrawScripting/DebugDrawScripting.hpp"

namespace Fenrir::Testing
{
    // A frame too short to run a tick.
    inline constexpr double debugDrawNoTick = oneTick * 0.25;

    // The fixture's types the tests call.
    inline constexpr const char* ShapeCallsType = "DebugDrawScriptFixture.ShapeCalls";
    inline constexpr const char* TextCallsType = "DebugDrawScriptFixture.TextCalls";
    inline constexpr const char* ItemCallsType = "DebugDrawScriptFixture.ItemCalls";
    inline constexpr const char* TickDrawerType = "DebugDrawScriptFixture.TickDrawer";

    // Debug drawing, with a Render-phase system keeping what the active scene's DebugDraw tessellates to in `seen`.
    struct DebugDrawPackages
    {
        explicit DebugDrawPackages(App& app) : drawing(DebugDrawing::Install(app))
        {
            app.AddSequentialSystem(SchedulePriority::Render, [this](App& running) {
                if (const DebugDraw* draw = running.GetActiveScene().GetResource<DebugDraw>())
                {
                    seen = draw->Tessellate();
                }
            });
        }

        DebugDrawing drawing;
        DebugBatches seen;
    };

    // An App with debug drawing installed and scripting running the fixture with the debug draw extension.
    struct DebugDrawScriptingApp : ExtensionTestApp<DebugDrawPackages>
    {
        DebugDrawScriptingApp()
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_DEBUGDRAW_FIXTURE"),
                               {DebugDrawing::ScriptingExtension()})
        {
        }

        // The active scene's DebugDraw.
        DebugDraw& Draw()
        {
            DebugDraw* draw = headless.Get().GetActiveScene().GetResource<DebugDraw>();
            REQUIRE(draw != nullptr);
            return *draw;
        }

        // Runs one frame of `deltaTime` seconds and returns what its Render phase saw.
        const DebugBatches& Frame(double deltaTime = oneTick)
        {
            seen = {};
            headless.Get().RunFrame(deltaTime);
            return seen;
        }
    };

    // How many lines every line batch holds together.
    inline std::size_t LineCount(const DebugBatches& batches)
    {
        std::size_t count = 0;
        for (const DebugLineBatch& batch : batches.lines)
        {
            count += batch.vertices.size() / 2;
        }
        return count;
    }
} // namespace Fenrir::Testing
