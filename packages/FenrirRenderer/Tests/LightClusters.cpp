// Light clustering (ADR-0049): the cluster grid's slices, boxes and light spheres on the CPU against brute force, and
// on the null backend through RendererProbe (the spec's seam 1) a LightClusters compute pass per view, its job, and the
// view set binding the view's own clusters to its lit passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Core/LightClusters.hpp"
#include "Core/SceneFrame.hpp"
#include "RendererTestApp.hpp"

using Catch::Approx;
using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::BindingGroupHandle;
using Fenrir::Graphics::BufferHandle;
using Fenrir::Math::Mat4;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using Fenrir::Render::ClusterBox;
using Fenrir::Render::ClusterCell;
using Fenrir::Render::ClusterCount;
using Fenrir::Render::ClusterDepths;
using Fenrir::Render::ClusterSlices;
using Fenrir::Render::GpuLight;
using Fenrir::Render::LightKind;
using Fenrir::Render::LightSphere;
using Fenrir::Render::MaxClusterLights;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A 60-degree 16:9 perspective from 0.1, as a camera's. */
    Mat4 Perspective()
    {
        return Fenrir::Render::ReversedZPerspective(Fenrir::Math::DegToRad(60.0f), 16.0f / 9.0f, 0.1f);
    }

    /** @brief An orthographic projection 10 units high, 16:9, from 0.5 to 40. */
    Mat4 Orthographic()
    {
        return Fenrir::Render::ReversedZOrthographic(10.0f, 16.0f / 9.0f, 0.5f, 40.0f);
    }

    /** @brief A view-space point in the frustum of `projection`, at NDC (x, y) and the view depth `depth`. */
    Vec3 InFrustum(const Mat4& projection, float x, float y, float depth)
    {
        // Along the line of sight through two depths, independently of the code under test's own.
        const Mat4 inverse = glm::inverse(projection);
        const Vec4 a = inverse * Vec4(x, y, 1.0f, 1.0f);
        const Vec4 b = inverse * Vec4(x, y, 0.25f, 1.0f);
        const Vec3 near = Vec3(a) / a.w;
        const Vec3 far = Vec3(b) / b.w;
        return near + (far - near) * ((-depth - near.z) / (far.z - near.z));
    }

    /** @brief How far `point` is from `box`, by brute force over a grid of `steps` points a side through it. */
    float SampledDistance(const ClusterBox& box, Vec3 point, int steps)
    {
        float nearest = INFINITY;
        for (int i = 0; i <= steps; ++i)
        {
            for (int j = 0; j <= steps; ++j)
            {
                for (int k = 0; k <= steps; ++k)
                {
                    const Vec3 t(static_cast<float>(i), static_cast<float>(j), static_cast<float>(k));
                    const Vec3 at = box.min + (box.max - box.min) * t / static_cast<float>(steps);
                    nearest = std::min(nearest, glm::length(at - point));
                }
            }
        }
        return nearest;
    }

    /** @brief A point light record at `position` reaching `range`. */
    GpuLight PointRecord(Vec3 position, float range)
    {
        GpuLight light;
        light.position = position;
        light.range = range;
        light.colour = Vec3(1.0f);
        return light;
    }

    /** @brief A rect light record at `position`, `halfWidth` along +X and `halfHeight` along +Y, reaching `range`. */
    GpuLight RectRecord(Vec3 position, float halfWidth, float halfHeight, float range)
    {
        GpuLight light = PointRecord(position, range);
        light.kind = static_cast<std::uint32_t>(LightKind::Rect);
        light.halfWidth = {halfWidth, 0.0f, 0.0f};
        light.halfHeight = {0.0f, halfHeight, 0.0f};
        return light;
    }

    /**
     * @brief Whether `light` lights the world point `point` at all, as Fenrir/Lighting.glsl's windows decide: nearer
     * than its range to a point or spot light's position, or to a rect light's nearest point.
     */
    bool Reaches(const GpuLight& light, Vec3 point)
    {
        Vec3 offset = point - light.position;
        if (light.kind == static_cast<std::uint32_t>(LightKind::Rect))
        {
            const float across = std::clamp(Fenrir::Math::Dot(offset, light.halfWidth) /
                                                Fenrir::Math::Dot(light.halfWidth, light.halfWidth),
                                            -1.0f, 1.0f);
            const float along = std::clamp(Fenrir::Math::Dot(offset, light.halfHeight) /
                                               Fenrir::Math::Dot(light.halfHeight, light.halfHeight),
                                           -1.0f, 1.0f);
            offset -= light.halfWidth * across + light.halfHeight * along;
        }
        return Fenrir::Math::Dot(offset, offset) < light.range * light.range;
    }

    /** @brief A PointLight entity at `position` reaching `range`. */
    Fenrir::Entity AddPointLight(RendererTestApp& test, Vec3 position, float range = 2.0f)
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        Fenrir::PointLight light;
        light.range = range;
        entity.AddComponent<Fenrir::PointLight>(light);
        return entity;
    }

    /** @brief A LightClusters pass's job block, as `light_clusters.comp` declares it. */
    struct ClusterJob
    {
        Mat4 view;
        Mat4 inverseProjection;
        Vec4 depths;
        std::array<std::uint32_t, 4> counts;
    };

    /** @brief One dispatch of the last frame's LightClusters passes: its job and its groups. */
    struct ClusterDispatch
    {
        ClusterJob job{};
        std::array<BindingGroupHandle, 4> groups{};
        Fenrir::Graphics::CmdDispatch dispatch;
    };

    /** @brief Every dispatch of the last frame, in order, with its job block as the device holds it. */
    std::vector<ClusterDispatch> Dispatches(const RendererTestApp& test)
    {
        using namespace Fenrir::Graphics;
        std::vector<ClusterDispatch> dispatches;
        for (const CommandList& list : RendererProbe::Submitted(test.renderer))
        {
            std::array<BindingGroupHandle, 4> groups{};
            for (const Command& command : list.commands)
            {
                if (const auto* group = std::get_if<CmdSetBindingGroup>(&command))
                {
                    groups.at(group->set) = group->group;
                }
                else if (const auto* dispatch = std::get_if<CmdDispatch>(&command))
                {
                    ClusterDispatch recorded{{}, groups, *dispatch};
                    const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), groups[0]);
                    REQUIRE(entries.size() == 1);
                    const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
                    REQUIRE(entries[0].offset + sizeof(ClusterJob) <= bytes.size());
                    std::memcpy(&recorded.job, std::span(bytes).subspan(entries[0].offset).data(), sizeof(ClusterJob));
                    dispatches.push_back(recorded);
                }
            }
        }
        return dispatches;
    }

    /** @brief The buffer a group binds at `binding`; empty if it binds none there. */
    BufferHandle BufferAt(const RendererTestApp& test, BindingGroupHandle group, std::uint32_t binding)
    {
        for (const BindingGroupEntry& entry : RendererProbe::Entries(test.Device(), group))
        {
            if (entry.binding == binding)
            {
                return entry.buffer;
            }
        }
        return {};
    }

    /** @brief Where a view block's clusterInfo sits: after the 608 bytes before ticket 19 and clusterDepths. */
    constexpr std::size_t ClusterInfoOffset = 624;

    /** @brief The x and z of clusterInfo in the view block a draw bound at set 1. */
    std::array<std::uint32_t, 2> ClusterInfoOf(const RendererTestApp& test, const RecordedDraw& draw)
    {
        const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), draw.groups.at(1));
        const auto block = std::find_if(entries.begin(), entries.end(),
                                        [](const BindingGroupEntry& entry) { return entry.binding == 0; });
        REQUIRE(block != entries.end());
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), block->buffer);
        return {RendererTest::UintAt(bytes, block->offset + ClusterInfoOffset),
                RendererTest::UintAt(bytes, block->offset + ClusterInfoOffset + 8)};
    }
} // namespace

TEST_CASE("Light clusters' slices run exponentially from the near plane to the far depth", "[renderer][light-clusters]")
{
    const ClusterDepths depths{0.1f, 100.0f};
    REQUIRE(Fenrir::Render::ClusterSliceDepth(0, depths) == Approx(0.1f));
    REQUIRE(Fenrir::Render::ClusterSliceDepth(ClusterSlices, depths) == Approx(100.0f));
    INFO("each slice a constant ratio deeper than the one before");
    const float ratio = std::pow(1000.0f, 1.0f / static_cast<float>(ClusterSlices));
    for (std::uint32_t slice = 0; slice < ClusterSlices; ++slice)
    {
        CAPTURE(slice);
        const float from = Fenrir::Render::ClusterSliceDepth(slice, depths);
        const float to = Fenrir::Render::ClusterSliceDepth(slice + 1, depths);
        REQUIRE(to / from == Approx(ratio).epsilon(1e-4));
        INFO("a depth inside the slice is in it");
        REQUIRE(Fenrir::Render::ClusterSliceOf(std::sqrt(from * to), depths) == slice);
    }
    INFO("nearer than the near plane is the first slice, past the far depth the last");
    REQUIRE(Fenrir::Render::ClusterSliceOf(0.01f, depths) == 0);
    REQUIRE(Fenrir::Render::ClusterSliceOf(500.0f, depths) == ClusterSlices - 1);
}

TEST_CASE("A cluster's index and cell turn into each other, column first", "[renderer][light-clusters]")
{
    REQUIRE(ClusterCount == 16 * 9 * 24);
    for (std::uint32_t index = 0; index < ClusterCount; ++index)
    {
        const ClusterCell cell = Fenrir::Render::ClusterCellOf(index);
        REQUIRE(cell.column < 16);
        REQUIRE(cell.row < 9);
        REQUIRE(cell.slice < 24);
        REQUIRE(Fenrir::Render::ClusterIndex(cell) == index);
    }
    REQUIRE(Fenrir::Render::ClusterIndex({1, 0, 0}) == 1);
    REQUIRE(Fenrir::Render::ClusterIndex({0, 1, 0}) == 16);
    REQUIRE(Fenrir::Render::ClusterIndex({0, 0, 1}) == 144);
}

TEST_CASE("Every point of a view's frustum is inside the box of the cluster it is found in",
          "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    const ClusterDepths depths{std::max(Fenrir::Render::DepthRangeOf(projection).near, 1e-3f), 30.0f};
    const Mat4 inverse = glm::inverse(projection);
    std::mt19937 random(19);
    std::uniform_real_distribution<float> ndc(-0.999f, 0.999f);
    std::uniform_real_distribution<float> logDepth(std::log(depths.near * 1.001f), std::log(depths.far * 0.999f));
    // A tolerance for float rounding, relative to the depth.
    for (int sample = 0; sample < 20000; ++sample)
    {
        const float depth = std::exp(logDepth(random));
        const Vec3 point = InFrustum(projection, ndc(random), ndc(random), depth);
        const ClusterCell cell = Fenrir::Render::ClusterOf(point, projection, depths);
        const ClusterBox box = Fenrir::Render::ClusterBoundsOf(inverse, depths, cell);
        CAPTURE(sample, point.x, point.y, point.z, cell.column, cell.row, cell.slice);
        const float slack = 1e-4f * depth;
        REQUIRE(glm::all(glm::greaterThanEqual(point, box.min - slack)));
        REQUIRE(glm::all(glm::lessThanEqual(point, box.max + slack)));
    }
}

TEST_CASE("A cluster's box spans its slice's depths and no more than its tile's frustum", "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    const ClusterDepths depths{std::max(Fenrir::Render::DepthRangeOf(projection).near, 1e-3f), 30.0f};
    const Mat4 inverse = glm::inverse(projection);
    for (const ClusterCell cell : {ClusterCell{0, 0, 0}, ClusterCell{7, 4, 11}, ClusterCell{15, 8, 23}})
    {
        CAPTURE(cell.column, cell.row, cell.slice);
        const ClusterBox box = Fenrir::Render::ClusterBoundsOf(inverse, depths, cell);
        const float from = Fenrir::Render::ClusterSliceDepth(cell.slice, depths);
        const float to = Fenrir::Render::ClusterSliceDepth(cell.slice + 1, depths);
        REQUIRE(-box.max.z == Approx(from).epsilon(1e-4));
        REQUIRE(-box.min.z == Approx(to).epsilon(1e-4));
        INFO("its x and y those of its tile's corners at the slice's depths");
        const float x0 = -1.0f + 2.0f * static_cast<float>(cell.column) / 16.0f;
        const float x1 = x0 + 2.0f / 16.0f;
        const float y0 = -1.0f + 2.0f * static_cast<float>(cell.row) / 9.0f;
        const float y1 = y0 + 2.0f / 9.0f;
        Vec3 low(INFINITY);
        Vec3 high(-INFINITY);
        for (const float depth : {from, to})
        {
            for (const Vec3 point : {InFrustum(projection, x0, y0, depth), InFrustum(projection, x1, y1, depth)})
            {
                low = glm::min(low, point);
                high = glm::max(high, point);
            }
        }
        REQUIRE(box.min.x == Approx(low.x).margin(1e-4));
        REQUIRE(box.min.y == Approx(low.y).margin(1e-4));
        REQUIRE(box.max.x == Approx(high.x).margin(1e-4));
        REQUIRE(box.max.y == Approx(high.y).margin(1e-4));
    }
}

TEST_CASE("A light's sphere touches a box as far as brute force finds it", "[renderer][light-clusters]")
{
    std::mt19937 random(91);
    std::uniform_real_distribution<float> coordinate(-2.0f, 2.0f);
    std::uniform_real_distribution<float> size(0.1f, 2.0f);
    std::uniform_real_distribution<float> radius(0.05f, 2.0f);
    constexpr int Steps = 40;
    int touching = 0;
    for (int sample = 0; sample < 400; ++sample)
    {
        ClusterBox box;
        box.min = {coordinate(random), coordinate(random), coordinate(random)};
        box.max = box.min + Vec3(size(random), size(random), size(random));
        const LightSphere sphere{{coordinate(random), coordinate(random), coordinate(random)}, radius(random)};
        const float sampled = SampledDistance(box, sphere.centre, Steps);
        // The grid's points are at most half a cell's diagonal from any point of the box.
        const float step = glm::length(box.max - box.min) / static_cast<float>(Steps);
        CAPTURE(sample, sampled, sphere.radius);
        const bool touches = Fenrir::Render::SphereTouchesBox(sphere, box);
        if (sampled <= sphere.radius)
        {
            REQUIRE(touches);
        }
        else if (sampled > sphere.radius + step)
        {
            REQUIRE_FALSE(touches);
        }
        touching += touches ? 1 : 0;
    }
    INFO("both outcomes are tried: " << touching << " of 400 touch");
    REQUIRE(touching > 50);
    REQUIRE(touching < 350);
}

TEST_CASE("A rect light's sphere reaches its range past the rect's corners", "[renderer][light-clusters]")
{
    const GpuLight rect = RectRecord({1.0f, 2.0f, -3.0f}, 3.0f, 4.0f, 2.0f);
    const LightSphere sphere = Fenrir::Render::LightSphereOf(rect, Mat4(1.0f));
    REQUIRE(sphere.centre == Vec3(1.0f, 2.0f, -3.0f));
    REQUIRE(sphere.radius == Approx(7.0f));
    INFO("a point or spot light's is its range");
    REQUIRE(Fenrir::Render::LightSphereOf(PointRecord({0.0f, 0.0f, 0.0f}, 2.5f), Mat4(1.0f)).radius == 2.5f);
}

TEST_CASE("The slices reach as far as the farthest light, held to an orthographic projection's far plane",
          "[renderer][light-clusters]")
{
    const std::array<LightSphere, 2> spheres{{{{0.0f, 0.0f, -10.0f}, 3.0f}, {{5.0f, 0.0f, -50.0f}, 2.0f}}};
    const ClusterDepths perspective = Fenrir::Render::ClusterDepthsOf(Perspective(), spheres);
    REQUIRE(perspective.near == Approx(0.1f));
    REQUIRE(perspective.far == Approx(52.0f));
    const ClusterDepths orthographic = Fenrir::Render::ClusterDepthsOf(Orthographic(), spheres);
    REQUIRE(orthographic.near == Approx(0.5f));
    REQUIRE(orthographic.far == Approx(40.0f));
    INFO("with no lights, a little past the near plane");
    const ClusterDepths none = Fenrir::Render::ClusterDepthsOf(Perspective(), {});
    REQUIRE(none.far > none.near);
}

TEST_CASE("Every light that reaches a point of the view is listed in that point's cluster",
          "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    // A camera at (0, 1, 5) turned a little, over lights of every kind scattered in front of it.
    const Mat4 view = glm::inverse(glm::translate(Mat4(1.0f), Vec3(0.0f, 1.0f, 5.0f)) *
                                   glm::rotate(Mat4(1.0f), 0.3f, Vec3(0.0f, 1.0f, 0.0f)));
    std::mt19937 random(7);
    std::uniform_real_distribution<float> across(-8.0f, 8.0f);
    std::uniform_real_distribution<float> range(0.3f, 3.0f);
    std::vector<GpuLight> lights;
    for (int i = 0; i < 200; ++i)
    {
        const Vec3 position(across(random), across(random) * 0.5f, -across(random) * 2.0f);
        if (i % 3 == 2)
        {
            lights.push_back(RectRecord(position, range(random) * 0.5f, range(random) * 0.5f, range(random)));
        }
        else
        {
            GpuLight light = PointRecord(position, range(random));
            light.kind = static_cast<std::uint32_t>(i % 3 == 0 ? LightKind::Point : LightKind::Spot);
            lights.push_back(light);
        }
    }
    const Fenrir::Render::ClusterAssignment assignment = Fenrir::Render::AssignClusters(lights, view, projection);
    for (const std::vector<std::uint32_t>& listed : assignment.lights)
    {
        REQUIRE(std::is_sorted(listed.begin(), listed.end()));
    }

    const Mat4 world = glm::inverse(view);
    std::uniform_real_distribution<float> ndc(-0.999f, 0.999f);
    std::uniform_real_distribution<float> logDepth(std::log(assignment.depths.near * 1.001f),
                                                   std::log(assignment.depths.far * 0.999f));
    std::size_t reached = 0;
    for (int sample = 0; sample < 20000; ++sample)
    {
        const Vec3 point = InFrustum(projection, ndc(random), ndc(random), std::exp(logDepth(random)));
        const Vec3 worldPoint = Vec3(world * Vec4(point, 1.0f));
        const std::vector<std::uint32_t>& listed = assignment.lights.at(
            Fenrir::Render::ClusterIndex(Fenrir::Render::ClusterOf(point, projection, assignment.depths)));
        for (std::uint32_t i = 0; i < lights.size(); ++i)
        {
            if (Reaches(lights[i], worldPoint))
            {
                ++reached;
                CAPTURE(sample, i);
                REQUIRE(std::binary_search(listed.begin(), listed.end(), i));
            }
        }
    }
    INFO("the samples are lit often enough to mean something: " << reached);
    REQUIRE(reached > 1000);

    INFO("and the lists are far shorter than the light buffer");
    std::size_t total = 0;
    for (const std::vector<std::uint32_t>& listed : assignment.lights)
    {
        total += listed.size();
    }
    REQUIRE(total < ClusterCount * lights.size() / 10);
}

TEST_CASE("A cluster lists its first lights up to its room, in light buffer order", "[renderer][light-clusters]")
{
    // 300 lights round one point, all reaching it.
    std::vector<GpuLight> lights(300, PointRecord({0.0f, 0.0f, -5.0f}, 1.0f));
    const Fenrir::Render::ClusterAssignment assignment =
        Fenrir::Render::AssignClusters(lights, Mat4(1.0f), Perspective());
    const std::vector<std::uint32_t>& listed = assignment.lights.at(
        Fenrir::Render::ClusterIndex(Fenrir::Render::ClusterOf({0.0f, 0.0f, -5.0f}, Perspective(), assignment.depths)));
    REQUIRE(listed.size() == MaxClusterLights);
    for (std::uint32_t i = 0; i < MaxClusterLights; ++i)
    {
        REQUIRE(listed[i] == i);
    }
}

TEST_CASE("A thousand lights are clustered once per view by a LightClusters pass before its Opaque pass",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddCamera({4.0f, 1.0f, 10.0f});
    test.AddMeshRenderer(test.Mesh(), test.Material());
    for (int i = 0; i < 1000; ++i)
    {
        AddPointLight(test, {static_cast<float>(i % 40) - 20.0f, 0.5f, -static_cast<float>(i / 40)});
    }
    test.Frame();

    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("one pass a view");
    REQUIRE(std::count(passes.begin(), passes.end(), "LightClusters") == 2);
    INFO("each before its view's Opaque pass");
    const auto first = std::find(passes.begin(), passes.end(), "LightClusters");
    REQUIRE(first < std::find(passes.begin(), passes.end(), "Opaque"));

    const std::vector<ClusterDispatch> dispatches = Dispatches(test);
    REQUIRE(dispatches.size() == 2);
    for (const ClusterDispatch& dispatch : dispatches)
    {
        INFO("an invocation a cluster, in groups of 64, over every light, with room for 256 a cluster");
        REQUIRE(dispatch.dispatch.groupsX == ClusterCount / 64);
        REQUIRE(dispatch.job.counts[0] == 1000);
        REQUIRE(dispatch.job.counts[1] == MaxClusterLights);
        INFO("its slices from the camera's near plane to the farthest light's reach");
        REQUIRE(dispatch.job.depths.x == Approx(0.1f));
        REQUIRE(dispatch.job.depths.y > 30.0f);
        REQUIRE(RendererProbe::Label(test.Device(), BufferAt(test, dispatch.groups[1], 1)) == "Light clusters");
    }
    INFO("each view's lists in a buffer of its own, of each cluster's count and its room");
    const BufferHandle firstClusters = BufferAt(test, dispatches[0].groups[1], 1);
    const BufferHandle secondClusters = BufferAt(test, dispatches[1].groups[1], 1);
    REQUIRE(firstClusters != secondClusters);
    REQUIRE(RendererProbe::Bytes(test.Device(), firstClusters).size() == ClusterCount * (1 + MaxClusterLights) * 4);
    INFO("from different cameras");
    REQUIRE(dispatches[0].job.view != dispatches[1].job.view);

    INFO("which each view's Opaque draws bind at set 1 binding 3, its view block saying so");
    const std::vector<RecordedDraw> draws = test.Draws("Opaque");
    REQUIRE(draws.size() == 2);
    std::vector<BufferHandle> bound;
    for (const RecordedDraw& draw : draws)
    {
        bound.push_back(BufferAt(test, draw.groups.at(1), 3));
        REQUIRE(ClusterInfoOf(test, draw) == std::array<std::uint32_t, 2>{1, MaxClusterLights});
    }
    REQUIRE(bound == std::vector<BufferHandle>{firstClusters, secondClusters});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A cluster's room is the light count when fewer lights than its most", "[renderer][light-clusters]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());
    AddPointLight(test, {0.0f, 1.0f, 0.0f});
    AddPointLight(test, {1.0f, 1.0f, 0.0f});
    AddPointLight(test, {2.0f, 1.0f, 0.0f});
    test.Frame();

    const std::vector<ClusterDispatch> dispatches = Dispatches(test);
    REQUIRE(dispatches.size() == 1);
    REQUIRE(dispatches[0].job.counts[0] == 3);
    REQUIRE(dispatches[0].job.counts[1] == 3);
    REQUIRE(RendererProbe::Bytes(test.Device(), BufferAt(test, dispatches[0].groups[1], 1)).size() ==
            ClusterCount * 4 * 4);
    REQUIRE(ClusterInfoOf(test, test.Draws().at(0)) == std::array<std::uint32_t, 2>{1, 3});
}

TEST_CASE("Without lights, or with clustering off, a view loops over the light buffer with no LightClusters pass",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());
    const bool lights = GENERATE(false, true);
    CAPTURE(lights);
    if (lights)
    {
        AddPointLight(test, {0.0f, 1.0f, 0.0f});
        test.renderer.SetClusteredLights(false);
    }
    test.Frame();

    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    REQUIRE(std::count(passes.begin(), passes.end(), "LightClusters") == 0);
    REQUIRE(Dispatches(test).empty());
    const RecordedDraw draw = test.Draws().at(0);
    INFO("set 1 binds the buffer of one 0 there, and the view block says to loop over every light");
    REQUIRE(RendererProbe::Label(test.Device(), BufferAt(test, draw.groups.at(1), 3)) == "No light clusters");
    REQUIRE(ClusterInfoOf(test, draw)[0] == 0);

    if (lights)
    {
        INFO("turned on again, the next frame clusters");
        test.renderer.SetClusteredLights(true);
        test.Frame();
        REQUIRE(Dispatches(test).size() == 1);
    }
}
