#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "DebugDrawTestApp.hpp"

using DebugDrawTest::TestApp;
using Fenrir::Colour;
using Fenrir::DebugDraw;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec3;

TEST_CASE("Items drawn by parallel systems are merged in registration order", "[debugdraw][threads]")
{
    TestApp test;
    // Each parallel system draws a line ending at x = its registration index; the first is the slowest, so it would
    // finish last if items were recorded as they were drawn.
    for (int index = 0; index < 4; ++index)
    {
        test.app.AddSystem(SchedulePriority::Update, [index](Fenrir::App& app) {
            if (index == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            DebugDraw& draw = *app.GetActiveScene().GetResource<DebugDraw>();
            for (int repeat = 0; repeat < 50; ++repeat)
            {
                draw.Line(Vec3(0.0f), Vec3(static_cast<float>(index), 0.0f, 0.0f), Colour::Red);
            }
        });
    }
    // A sequential system in the same phase draws after the parallel batch has merged.
    test.app.AddSequentialSystem(SchedulePriority::Update, [](Fenrir::App& app) {
        app.GetActiveScene().GetResource<DebugDraw>()->Line(Vec3(0.0f), Vec3(9.0f, 0.0f, 0.0f), Colour::Red);
    });
    test.WatchRender();

    const Fenrir::DebugBatches& seen = test.FrameWithNoTick();

    REQUIRE(seen.lines.size() == 1);
    const std::vector<Fenrir::DebugVertex>& vertices = seen.lines[0].vertices;
    INFO("every line is recorded: four systems drawing 50 each, and one more");
    REQUIRE(vertices.size() == (4 * 50 + 1) * 2);
    for (std::size_t line = 0; line < 4 * 50; ++line)
    {
        INFO("line " << line << " comes from the system registered " << line / 50 << "th");
        REQUIRE(static_cast<std::size_t>(vertices[line * 2 + 1].position.x) == line / 50);
    }
    INFO("the sequential system's line comes last");
    REQUIRE(vertices.back().position.x == 9.0f);
}
