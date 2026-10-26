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

namespace DebugDrawScriptingTests
{
    using Fenrir::Testing::oneTick;

    // A frame too short to run a tick.
    inline constexpr double noTick = oneTick * 0.25;

    // The fixture's types the tests call.
    inline constexpr const char* ShapeCallsType = "DebugDrawScriptFixture.ShapeCalls";
    inline constexpr const char* TextCallsType = "DebugDrawScriptFixture.TextCalls";
    inline constexpr const char* ItemCallsType = "DebugDrawScriptFixture.ItemCalls";
    inline constexpr const char* TickDrawerType = "DebugDrawScriptFixture.TickDrawer";

    // Debug drawing, with a Render-phase system keeping what the active scene's DebugDraw tessellates to in `seen`.
    struct DebugDrawPackages
    {
        explicit DebugDrawPackages(Fenrir::App& app) : drawing(Fenrir::DebugDrawing::Install(app))
        {
            app.AddSequentialSystem(Fenrir::SchedulePriority::Render, [this](Fenrir::App& running) {
                if (const Fenrir::DebugDraw* draw = running.GetActiveScene().GetResource<Fenrir::DebugDraw>())
                {
                    seen = draw->Tessellate();
                }
            });
        }

        Fenrir::DebugDrawing drawing;
        Fenrir::DebugBatches seen;
    };

    // An App with debug drawing installed and scripting running the fixture with the debug draw extension.
    struct DebugDrawScriptingApp : Fenrir::Testing::ExtensionTestApp<DebugDrawPackages>
    {
        DebugDrawScriptingApp()
            : ExtensionTestApp(Fenrir::Testing::FixtureFromEnvironment("FENRIR_TEST_DEBUGDRAW_FIXTURE"),
                               {Fenrir::DebugDrawing::ScriptingExtension()})
        {
        }

        // The active scene's DebugDraw.
        Fenrir::DebugDraw& Draw()
        {
            Fenrir::DebugDraw* draw = headless.Get().GetActiveScene().GetResource<Fenrir::DebugDraw>();
            REQUIRE(draw != nullptr);
            return *draw;
        }

        // Runs one frame of `deltaTime` seconds and returns what its Render phase saw.
        const Fenrir::DebugBatches& Frame(double deltaTime = oneTick)
        {
            seen = {};
            headless.Get().RunFrame(deltaTime);
            return seen;
        }
    };

    // How many lines every line batch holds together.
    inline std::size_t LineCount(const Fenrir::DebugBatches& batches)
    {
        std::size_t count = 0;
        for (const Fenrir::DebugLineBatch& batch : batches.lines)
        {
            count += batch.vertices.size() / 2;
        }
        return count;
    }
} // namespace DebugDrawScriptingTests
