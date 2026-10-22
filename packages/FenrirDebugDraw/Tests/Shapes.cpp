#include <catch2/catch_test_macros.hpp>

#include "DebugDrawTestApp.hpp"

using DebugDrawTest::SameSegments;
using DebugDrawTest::Segments;
using DebugDrawTest::TestApp;
using Fenrir::Colour;
using Fenrir::DepthMode;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec3;

TEST_CASE("A line drawn in Update reaches that frame's Render as one line batch", "[debugdraw][shapes]")
{
    TestApp test;
    test.app.AddSequentialSystem(SchedulePriority::Update, [](Fenrir::App& app) {
        app.GetActiveScene().GetResource<Fenrir::DebugDraw>()->Line(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f),
                                                                    Colour::Red);
    });
    test.WatchRender();

    const Fenrir::DebugBatches& seen = test.Frame(DebugDrawTest::TickRate);

    INFO("one line batch is drawn, and nothing else");
    REQUIRE(seen.lines.size() == 1);
    REQUIRE(seen.triangles.empty());
    REQUIRE(seen.texts.empty());
    const Fenrir::DebugLineBatch& batch = seen.lines[0];
    INFO("the batch has the defaults: depth tested, 1 pixel thick");
    REQUIRE(batch.depth == DepthMode::Tested);
    REQUIRE(batch.thickness == 1.0f);
    INFO("its two vertices are the line's ends, in the line's colour");
    REQUIRE(batch.vertices.size() == 2);
    REQUIRE(batch.vertices[0].position == Vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(batch.vertices[1].position == Vec3(4.0f, 5.0f, 6.0f));
    REQUIRE(batch.vertices[0].colour.r == 1.0f);
    REQUIRE(batch.vertices[0].colour.g == 0.0f);
    REQUIRE(batch.vertices[1].colour.r == 1.0f);
}

namespace
{
    // Runs one frame in which `draw` draws into the active scene from Update, and returns what Render saw.
    template <typename Draw>
    Fenrir::DebugBatches DrawnInOneFrame(Draw draw)
    {
        TestApp test;
        test.app.AddSequentialSystem(SchedulePriority::Update, [&draw](Fenrir::App& app) {
            draw(*app.GetActiveScene().GetResource<Fenrir::DebugDraw>());
        });
        test.WatchRender();
        return test.Frame(DebugDrawTest::TickRate);
    }
} // namespace

TEST_CASE("A wire box is its twelve edges", "[debugdraw][shapes]")
{
    const Fenrir::DebugBatches seen = DrawnInOneFrame([](Fenrir::DebugDraw& draw) {
        draw.Box(Fenrir::Math::AABB(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 2.0f, 3.0f)), Colour::Green);
    });

    INFO("the box is one line batch");
    REQUIRE(seen.lines.size() == 1);
    // The corners of the box from (0, 0, 0) to (1, 2, 3), joined along each axis.
    const std::vector<DebugDrawTest::Segment> edges = {
        {{0, 0, 0}, {1, 0, 0}}, {{0, 2, 0}, {1, 2, 0}}, {{0, 0, 3}, {1, 0, 3}}, {{0, 2, 3}, {1, 2, 3}},
        {{0, 0, 0}, {0, 2, 0}}, {{1, 0, 0}, {1, 2, 0}}, {{0, 0, 3}, {0, 2, 3}}, {{1, 0, 3}, {1, 2, 3}},
        {{0, 0, 0}, {0, 0, 3}}, {{1, 0, 0}, {1, 0, 3}}, {{0, 2, 0}, {0, 2, 3}}, {{1, 2, 0}, {1, 2, 3}},
    };
    INFO("its lines are the box's twelve edges");
    REQUIRE(SameSegments(Segments(seen.lines[0]), edges));
}

TEST_CASE("A wire sphere is three circles of the given segment count", "[debugdraw][shapes]")
{
    const Fenrir::DebugBatches seen = DrawnInOneFrame(
        [](Fenrir::DebugDraw& draw) { draw.Sphere(Vec3(2.0f, 0.0f, 0.0f), 1.0f, Colour::White, {.segments = 4}); });

    INFO("the sphere is one line batch");
    REQUIRE(seen.lines.size() == 1);
    // With four segments each circle is a square through the four points a radius from the centre on its plane.
    const std::vector<DebugDrawTest::Segment> squares = {
        {{3, 0, 0}, {2, 1, 0}}, {{2, 1, 0}, {1, 0, 0}},  {{1, 0, 0}, {2, -1, 0}},  {{2, -1, 0}, {3, 0, 0}},
        {{3, 0, 0}, {2, 0, 1}}, {{2, 0, 1}, {1, 0, 0}},  {{1, 0, 0}, {2, 0, -1}},  {{2, 0, -1}, {3, 0, 0}},
        {{2, 1, 0}, {2, 0, 1}}, {{2, 0, 1}, {2, -1, 0}}, {{2, -1, 0}, {2, 0, -1}}, {{2, 0, -1}, {2, 1, 0}},
    };
    INFO("its lines are three squares, in the XY, XZ and YZ planes");
    REQUIRE(SameSegments(Segments(seen.lines[0]), squares));
}

TEST_CASE("An arrow is its shaft and four lines of head", "[debugdraw][shapes]")
{
    const Fenrir::DebugBatches seen = DrawnInOneFrame([](Fenrir::DebugDraw& draw) {
        draw.Arrow(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), 1.0f, Colour::Yellow);
    });

    INFO("the arrow is one line batch");
    REQUIRE(seen.lines.size() == 1);
    // A head 1 long: from the tip back to 1 along the shaft, spread 0.5 either side in Y and in Z.
    const std::vector<DebugDrawTest::Segment> arrow = {
        {{0, 0, 0}, {2, 0, 0}},    {{2, 0, 0}, {1, 0.5f, 0}},  {{2, 0, 0}, {1, -0.5f, 0}},
        {{2, 0, 0}, {1, 0, 0.5f}}, {{2, 0, 0}, {1, 0, -0.5f}},
    };
    INFO("its lines are the shaft and the four lines of the head");
    REQUIRE(SameSegments(Segments(seen.lines[0]), arrow));
}

TEST_CASE("A solid triangle list is one triangle batch of its vertices, with the colour's alpha", "[debugdraw][shapes]")
{
    const std::vector<Vec3> vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {9, 9, 9}};
    const Colour seeThrough{0.0f, 0.0f, 1.0f, 0.5f};
    const Fenrir::DebugBatches seen =
        DrawnInOneFrame([&](Fenrir::DebugDraw& draw) { draw.Triangles(vertices, seeThrough); });

    INFO("the triangles are one triangle batch, depth tested, and no lines");
    REQUIRE(seen.lines.empty());
    REQUIRE(seen.triangles.size() == 1);
    REQUIRE(seen.triangles[0].depth == DepthMode::Tested);
    INFO("the batch holds the two whole triangles in order; the odd vertex left over is not drawn");
    REQUIRE(seen.triangles[0].vertices.size() == 6);
    for (std::size_t i = 0; i < 6; ++i)
    {
        REQUIRE(seen.triangles[0].vertices[i].position == vertices[i]);
        REQUIRE(seen.triangles[0].vertices[i].colour.a == 0.5f);
    }
}

TEST_CASE("A solid box is twelve triangles", "[debugdraw][shapes]")
{
    const Fenrir::DebugBatches seen = DrawnInOneFrame([](Fenrir::DebugDraw& draw) {
        draw.SolidBox(Fenrir::Math::AABB(Vec3(-1.0f), Vec3(1.0f)), Colour::Red, {.depth = DepthMode::OnTop});
    });

    INFO("the box is one triangle batch, on top");
    REQUIRE(seen.triangles.size() == 1);
    REQUIRE(seen.triangles[0].depth == DepthMode::OnTop);
    INFO("two triangles per face, six faces");
    REQUIRE(seen.triangles[0].vertices.size() == 36);
}

TEST_CASE("Lines are batched by depth mode and thickness, in the order those first occur", "[debugdraw][shapes]")
{
    const Fenrir::DebugBatches seen = DrawnInOneFrame([](Fenrir::DebugDraw& draw) {
        draw.Line(Vec3(0.0f), Vec3(1.0f), Colour::Red, {.thickness = 3.0f, .depth = DepthMode::OnTop});
        draw.Line(Vec3(0.0f), Vec3(2.0f), Colour::Red);
        draw.Line(Vec3(0.0f), Vec3(3.0f), Colour::Red, {.thickness = 3.0f, .depth = DepthMode::OnTop});
    });

    INFO("two batches: thick on-top lines, then thin depth-tested ones");
    REQUIRE(seen.lines.size() == 2);
    REQUIRE(seen.lines[0].depth == DepthMode::OnTop);
    REQUIRE(seen.lines[0].thickness == 3.0f);
    REQUIRE(seen.lines[1].depth == DepthMode::Tested);
    REQUIRE(seen.lines[1].thickness == 1.0f);
    INFO("the on-top batch holds both thick lines, in the order drawn");
    REQUIRE(seen.lines[0].vertices.size() == 4);
    REQUIRE(seen.lines[0].vertices[1].position == Vec3(1.0f));
    REQUIRE(seen.lines[0].vertices[3].position == Vec3(3.0f));
    REQUIRE(seen.lines[1].vertices.size() == 2);
}

TEST_CASE("A point is a line of no length as thick as the point's pixel size", "[debugdraw][shapes]")
{
    const Fenrir::DebugBatches seen =
        DrawnInOneFrame([](Fenrir::DebugDraw& draw) { draw.Point(Vec3(1.0f, 2.0f, 3.0f), 8.0f, Colour::Cyan); });

    INFO("the point is one batch 8 pixels thick");
    REQUIRE(seen.lines.size() == 1);
    REQUIRE(seen.lines[0].thickness == 8.0f);
    INFO("both its vertices are the point");
    REQUIRE(seen.lines[0].vertices.size() == 2);
    REQUIRE(seen.lines[0].vertices[0].position == Vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(seen.lines[0].vertices[1].position == Vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("Axes are the transform's three axes in red, green and blue", "[debugdraw][shapes]")
{
    const Fenrir::Math::Mat4 frame = Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Vec3(1.0f, 0.0f, 0.0f));
    const Fenrir::DebugBatches seen = DrawnInOneFrame([&](Fenrir::DebugDraw& draw) { draw.Axes(frame, 2.0f); });

    REQUIRE(seen.lines.size() == 1);
    const std::vector<Fenrir::DebugVertex>& v = seen.lines[0].vertices;
    INFO("three lines from the origin (1, 0, 0), 2 long along X, Y and Z");
    REQUIRE(v.size() == 6);
    REQUIRE(v[1].position == Vec3(3.0f, 0.0f, 0.0f));
    REQUIRE(v[3].position == Vec3(1.0f, 2.0f, 0.0f));
    REQUIRE(v[5].position == Vec3(1.0f, 0.0f, 2.0f));
    INFO("coloured red, green and blue");
    REQUIRE((v[0].colour.r == 1.0f && v[0].colour.g == 0.0f));
    REQUIRE((v[2].colour.g == 1.0f && v[2].colour.r == 0.0f));
    REQUIRE((v[4].colour.b == 1.0f && v[4].colour.r == 0.0f));
}
