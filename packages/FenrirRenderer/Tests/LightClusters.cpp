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

#include "Core/Cascades.hpp"
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
using Fenrir::Render::ClusterAssignment;
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

    /** @brief The slices of `projection` out to 30 units, from its near plane held at least 1e-3. */
    ClusterDepths ThirtyUnits(const Mat4& projection)
    {
        return {std::max(Fenrir::Render::DepthRangeOf(projection).near, 1e-3f), 30.0f};
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

    /** @brief Whether `point` is inside `box`. */
    bool Inside(const ClusterBox& box, Vec3 point)
    {
        return glm::all(glm::greaterThanEqual(point, box.min)) && glm::all(glm::lessThanEqual(point, box.max));
    }

    /** @brief The box of the cluster `point` is found in, for `projection` and `depths`. */
    ClusterBox BoxFoundFor(const Mat4& projection, ClusterDepths depths, Vec3 point)
    {
        return Fenrir::Render::ClusterBoundsOf(glm::inverse(projection), depths,
                                               Fenrir::Render::ClusterOf(point, projection, depths));
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

    /** @brief A camera at (0, 1, 5) turned a little about +Y: world to its view. */
    Mat4 TurnedView()
    {
        return glm::inverse(glm::translate(Mat4(1.0f), Vec3(0.0f, 1.0f, 5.0f)) *
                            glm::rotate(Mat4(1.0f), 0.3f, Vec3(0.0f, 1.0f, 0.0f)));
    }

    /** @brief 200 lights scattered in front of TurnedView's camera, a third each point, spot and rect lights. */
    std::vector<GpuLight> ScatteredLights()
    {
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
        return lights;
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

    /** @brief Two cameras four units apart over a lit cube and a thousand point lights in rows behind it, one frame. */
    void ThousandLights(RendererTestApp& test)
    {
        test.AddCamera({0.0f, 1.0f, 10.0f});
        test.AddCamera({4.0f, 1.0f, 10.0f});
        test.AddMeshRenderer(test.Mesh(), test.Material());
        for (int i = 0; i < 1000; ++i)
        {
            AddPointLight(test, {static_cast<float>(i % 40) - 20.0f, 0.5f, -static_cast<float>(i / 40)});
        }
        test.Frame();
    }

    /** @brief One camera over a lit cube and `lights` point lights, one frame. */
    void FewLights(RendererTestApp& test, int lights)
    {
        test.AddCamera();
        test.AddMeshRenderer(test.Mesh(), test.Material());
        for (int i = 0; i < lights; ++i)
        {
            AddPointLight(test, {static_cast<float>(i), 1.0f, 0.0f});
        }
        test.Frame();
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
                    INFO("a dispatch binds its job block alone at set 0");
                    REQUIRE(entries.size() == 1);
                    const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
                    INFO("and the block fits its buffer");
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

    /** @brief Where a view block's clusterInfo sits: after the 608 bytes before ticket 19 and clusterSlices. */
    constexpr std::size_t ClusterInfoOffset = 624;

    /** @brief The x and z of clusterInfo in the view block a draw bound at set 1. */
    std::array<std::uint32_t, 2> ClusterInfoOf(const RendererTestApp& test, const RecordedDraw& draw)
    {
        const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), draw.groups.at(1));
        const auto block = std::find_if(entries.begin(), entries.end(),
                                        [](const BindingGroupEntry& entry) { return entry.binding == 0; });
        INFO("set 1 binds a view block at binding 0");
        REQUIRE(block != entries.end());
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), block->buffer);
        return {RendererTest::UintAt(bytes, block->offset + ClusterInfoOffset),
                RendererTest::UintAt(bytes, block->offset + ClusterInfoOffset + 8)};
    }

    /** @brief How many passes labelled LightClusters the last frame ran. */
    std::ptrdiff_t LightClustersPasses(const RendererTestApp& test)
    {
        const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
        return std::count(passes.begin(), passes.end(), "LightClusters");
    }
} // namespace

// ---- The grid, on the CPU ----

TEST_CASE("Each light cluster slice is a constant ratio deeper than the one before, from the near plane to the far "
          "depth",
          "[renderer][light-clusters]")
{
    const ClusterDepths depths{0.1f, 100.0f};
    INFO("the first slice starts at the near plane");
    REQUIRE(Fenrir::Render::ClusterSliceDepth(0, depths) == Approx(0.1f));
    INFO("the last ends at the far depth");
    REQUIRE(Fenrir::Render::ClusterSliceDepth(ClusterSlices, depths) == Approx(100.0f));
    const float ratio = std::pow(1000.0f, 1.0f / static_cast<float>(ClusterSlices));
    for (std::uint32_t slice = 0; slice < ClusterSlices; ++slice)
    {
        CAPTURE(slice);
        const float from = Fenrir::Render::ClusterSliceDepth(slice, depths);
        const float to = Fenrir::Render::ClusterSliceDepth(slice + 1, depths);
        INFO("each slice is (far / near)^(1 / 24) times as deep as the one before");
        REQUIRE(to / from == Approx(ratio).epsilon(1e-4));
    }
}

TEST_CASE("A view depth is found in the slice spanning it, the first nearer than the near plane and the last past the "
          "far depth",
          "[renderer][light-clusters]")
{
    const ClusterDepths depths{0.1f, 100.0f};
    for (std::uint32_t slice = 0; slice < ClusterSlices; ++slice)
    {
        CAPTURE(slice);
        const float middle = std::sqrt(Fenrir::Render::ClusterSliceDepth(slice, depths) *
                                       Fenrir::Render::ClusterSliceDepth(slice + 1, depths));
        INFO("a depth between a slice's faces is in that slice");
        REQUIRE(Fenrir::Render::ClusterSliceOf(middle, depths) == slice);
    }
    INFO("nearer than the near plane is the first slice");
    REQUIRE(Fenrir::Render::ClusterSliceOf(0.01f, depths) == 0);
    INFO("past the far depth is the last");
    REQUIRE(Fenrir::Render::ClusterSliceOf(500.0f, depths) == ClusterSlices - 1);
}

TEST_CASE("A cluster's index runs along columns, then rows, then slices, and turns back into its cell",
          "[renderer][light-clusters]")
{
    INFO("16 x 9 x 24 clusters");
    REQUIRE(ClusterCount == 16 * 9 * 24);
    for (std::uint32_t index = 0; index < ClusterCount; ++index)
    {
        CAPTURE(index);
        INFO("each index's cell gives the index back");
        REQUIRE(Fenrir::Render::ClusterIndex(Fenrir::Render::ClusterCellOf(index)) == index);
    }
    INFO("one column along is one index along, one row 16 and one slice 144");
    REQUIRE(Fenrir::Render::ClusterIndex({1, 0, 0}) == 1);
    REQUIRE(Fenrir::Render::ClusterIndex({0, 1, 0}) == 16);
    REQUIRE(Fenrir::Render::ClusterIndex({0, 0, 1}) == 144);
}

TEST_CASE("Every point of a view's frustum is inside the box of the cluster it is found in",
          "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    const ClusterDepths depths = ThirtyUnits(projection);
    std::mt19937 random(19);
    std::uniform_real_distribution<float> ndc(-1.0f, 1.0f);
    std::uniform_real_distribution<float> logDepth(std::log(depths.near), std::log(depths.far));
    for (int sample = 0; sample < 20000; ++sample)
    {
        const Vec3 point = InFrustum(projection, ndc(random), ndc(random), std::exp(logDepth(random)));
        CAPTURE(sample, point.x, point.y, point.z);
        INFO("the point is inside its cluster's box, boundaries and the viewport's edges included");
        REQUIRE(Inside(BoxFoundFor(projection, depths, point), point));
    }
}

TEST_CASE("A point just past the viewport's edge, as MSAA can interpolate one, is inside its edge tile's box",
          "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    const ClusterDepths depths = ThirtyUnits(projection);
    for (const Vec3 ndc :
         {Vec3(1.02f, 0.3f, 5.0f), Vec3(-1.02f, -0.5f, 12.0f), Vec3(0.2f, 1.02f, 2.0f), Vec3(-1.02f, -1.02f, 20.0f)})
    {
        const Vec3 point = InFrustum(projection, ndc.x, ndc.y, ndc.z);
        CAPTURE(ndc.x, ndc.y, ndc.z);
        INFO("the point is clamped into an edge tile whose box reaches it");
        REQUIRE(Inside(BoxFoundFor(projection, depths, point), point));
    }
}

TEST_CASE("An orthographic view's first slice starts at its near plane, even behind the camera",
          "[renderer][light-clusters]")
{
    // Its near plane two units behind the camera.
    const Mat4 projection = Fenrir::Render::ReversedZOrthographic(10.0f, 16.0f / 9.0f, -2.0f, 40.0f);
    const ClusterDepths depths = ThirtyUnits(projection);
    for (const float depth : {-1.9f, -0.5f, 0.0f, 0.0005f})
    {
        const Vec3 point = InFrustum(projection, 0.1f, -0.2f, depth);
        CAPTURE(depth);
        INFO("a visible point nearer than the slices' start is in the first slice");
        REQUIRE(Fenrir::Render::ClusterOf(point, projection, depths).slice == 0);
        INFO("whose box reaches back to it");
        REQUIRE(Inside(BoxFoundFor(projection, depths, point), point));
    }
}

TEST_CASE("A cluster's box spans its slice's depths and its tile's frustum, padded by a hundredth of its size",
          "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    const ClusterDepths depths = ThirtyUnits(projection);
    const Mat4 inverse = glm::inverse(projection);
    // Tiles away from the viewport's edges, which reach past it.
    for (const ClusterCell cell : {ClusterCell{7, 4, 11}, ClusterCell{3, 2, 5}, ClusterCell{14, 7, 23}})
    {
        CAPTURE(cell.column, cell.row, cell.slice);
        const ClusterBox box = Fenrir::Render::ClusterBoundsOf(inverse, depths, cell);
        const float from = Fenrir::Render::ClusterSliceDepth(cell.slice, depths);
        const float to = Fenrir::Render::ClusterSliceDepth(cell.slice + 1, depths);
        const float x0 = -1.0f + 2.0f * static_cast<float>(cell.column) / 16.0f;
        const float y0 = -1.0f + 2.0f * static_cast<float>(cell.row) / 9.0f;
        Vec3 low(INFINITY);
        Vec3 high(-INFINITY);
        for (const float depth : {from, to})
        {
            for (const Vec3 point : {InFrustum(projection, x0, y0, depth),
                                     InFrustum(projection, x0 + 2.0f / 16.0f, y0 + 2.0f / 9.0f, depth)})
            {
                low = glm::min(low, point);
                high = glm::max(high, point);
            }
        }
        const Vec3 padding = (high - low) * 0.01f;
        for (int axis = 0; axis < 3; ++axis)
        {
            CAPTURE(axis);
            INFO("the box's low face is the frustum segment's, a hundredth of its size further out");
            REQUIRE(box.min[axis] == Approx(low[axis] - padding[axis]).margin(1e-4));
            INFO("and so is its high face");
            REQUIRE(box.max[axis] == Approx(high[axis] + padding[axis]).margin(1e-4));
        }
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
            INFO("a box point brute force finds within the radius means they touch");
            REQUIRE(touches);
        }
        else if (sampled > sphere.radius + step)
        {
            INFO("no box point within the radius plus the grid's step means they do not");
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
    // Half sizes 3 and 4: corners 5 from the centre.
    const LightSphere sphere =
        Fenrir::Render::LightSphereOf(RectRecord({1.0f, 2.0f, -3.0f}, 3.0f, 4.0f, 2.0f), Mat4(1.0f));
    INFO("centred on the rect");
    REQUIRE(sphere.centre == Vec3(1.0f, 2.0f, -3.0f));
    INFO("its radius the corner's distance and the range");
    REQUIRE(sphere.radius == Approx(7.0f));
}

TEST_CASE("A point or spot light's sphere is its range about it, in view space", "[renderer][light-clusters]")
{
    const Mat4 view = glm::translate(Mat4(1.0f), Vec3(0.0f, 0.0f, -4.0f));
    const LightSphere sphere = Fenrir::Render::LightSphereOf(PointRecord({1.0f, 0.0f, 0.0f}, 2.5f), view);
    INFO("its position taken into the view");
    REQUIRE(sphere.centre == Vec3(1.0f, 0.0f, -4.0f));
    INFO("its radius its range");
    REQUIRE(sphere.radius == 2.5f);
}

TEST_CASE("A perspective view's slices reach from its near plane as far as the farthest light reaches",
          "[renderer][light-clusters]")
{
    const std::array<LightSphere, 2> spheres{{{{0.0f, 0.0f, -10.0f}, 3.0f}, {{5.0f, 0.0f, -50.0f}, 2.0f}}};
    const ClusterDepths depths = Fenrir::Render::ClusterDepthsOf(Perspective(), spheres);
    INFO("from the near plane");
    REQUIRE(depths.near == Approx(0.1f));
    INFO("to 50 + 2");
    REQUIRE(depths.far == Approx(52.0f));
}

TEST_CASE("An orthographic view's slices reach no farther than its far plane", "[renderer][light-clusters]")
{
    const std::array<LightSphere, 1> spheres{{{{0.0f, 0.0f, -50.0f}, 2.0f}}};
    const ClusterDepths depths = Fenrir::Render::ClusterDepthsOf(Orthographic(), spheres);
    INFO("from its near plane at 0.5");
    REQUIRE(depths.near == Approx(0.5f));
    INFO("to its far plane at 40, short of the light's 52");
    REQUIRE(depths.far == Approx(40.0f));
}

TEST_CASE("With no lights a view's slices still reach past the near plane", "[renderer][light-clusters]")
{
    const ClusterDepths depths = Fenrir::Render::ClusterDepthsOf(Perspective(), {});
    INFO("the slices keep a depth, so their logarithm stays finite");
    REQUIRE(depths.far > depths.near);
}

// ---- The assignment, on the CPU ----

TEST_CASE("Every light that reaches a point of the view is listed in that point's cluster",
          "[renderer][light-clusters]")
{
    const Mat4 projection = GENERATE(Perspective(), Orthographic());
    const Mat4 view = TurnedView();
    const std::vector<GpuLight> lights = ScatteredLights();
    const ClusterAssignment assignment = Fenrir::Render::AssignClusters(lights, view, projection);

    const Mat4 world = glm::inverse(view);
    std::mt19937 random(8);
    std::uniform_real_distribution<float> ndc(-1.0f, 1.0f);
    std::uniform_real_distribution<float> logDepth(std::log(assignment.depths.near), std::log(assignment.depths.far));
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
                INFO("a light reaching the point is in its cluster's list");
                REQUIRE(std::find(listed.begin(), listed.end(), i) != listed.end());
            }
        }
    }
    INFO("the samples are lit often enough to mean something: " << reached);
    REQUIRE(reached > 1000);
}

TEST_CASE("A cluster lists its lights in light buffer order", "[renderer][light-clusters]")
{
    const ClusterAssignment assignment = Fenrir::Render::AssignClusters(ScatteredLights(), TurnedView(), Perspective());
    for (const std::vector<std::uint32_t>& listed : assignment.lights)
    {
        INFO("each list is ascending, so its pixels add their lights in the same order as unclustered");
        REQUIRE(std::is_sorted(listed.begin(), listed.end()));
    }
}

TEST_CASE("Clusters list far fewer lights than the light buffer holds", "[renderer][light-clusters]")
{
    const std::vector<GpuLight> lights = ScatteredLights();
    const ClusterAssignment assignment = Fenrir::Render::AssignClusters(lights, TurnedView(), Perspective());
    std::size_t total = 0;
    for (const std::vector<std::uint32_t>& listed : assignment.lights)
    {
        total += listed.size();
    }
    INFO("the clusters list " << total << " lights in all, under a tenth of every cluster listing every light");
    REQUIRE(total < ClusterCount * lights.size() / 10);
}

TEST_CASE("A cluster lists its first lights up to its room, in light buffer order", "[renderer][light-clusters]")
{
    // 300 lights round one point, all reaching it.
    const std::vector<GpuLight> lights(300, PointRecord({0.0f, 0.0f, -5.0f}, 1.0f));
    const ClusterAssignment assignment = Fenrir::Render::AssignClusters(lights, Mat4(1.0f), Perspective());
    const std::vector<std::uint32_t>& listed = assignment.lights.at(
        Fenrir::Render::ClusterIndex(Fenrir::Render::ClusterOf({0.0f, 0.0f, -5.0f}, Perspective(), assignment.depths)));
    INFO("the cluster holds 256 of them");
    REQUIRE(listed.size() == MaxClusterLights);
    for (std::uint32_t i = 0; i < MaxClusterLights; ++i)
    {
        CAPTURE(i);
        INFO("the first 256 in the light buffer");
        REQUIRE(listed[i] == i);
    }
}

// ---- The passes, on the null backend ----

TEST_CASE("Each view with lights gets one LightClusters pass, before its Opaque pass", "[renderer][light-clusters]")
{
    RendererTestApp test;
    ThousandLights(test);
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("one pass for each of the two views");
    REQUIRE(LightClustersPasses(test) == 2);
    INFO("the first before the first Opaque pass");
    REQUIRE(std::find(passes.begin(), passes.end(), "LightClusters") <
            std::find(passes.begin(), passes.end(), "Opaque"));
}

TEST_CASE("A LightClusters pass runs an invocation a cluster over every light, with room for 256 lights a cluster",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    ThousandLights(test);
    const std::vector<ClusterDispatch> dispatches = Dispatches(test);
    INFO("one dispatch a view");
    REQUIRE(dispatches.size() == 2);
    for (const ClusterDispatch& dispatch : dispatches)
    {
        INFO("3456 invocations, in groups of 64");
        REQUIRE(dispatch.dispatch.groupsX == ClusterCount / 64);
        INFO("over all thousand lights");
        REQUIRE(dispatch.job.counts[0] == 1000);
        INFO("with room for 256 in each cluster");
        REQUIRE(dispatch.job.counts[1] == MaxClusterLights);
    }
}

TEST_CASE("A LightClusters pass's slices run from its camera's near plane to the farthest light's reach",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    ThousandLights(test);
    for (const ClusterDispatch& dispatch : Dispatches(test))
    {
        INFO("from the camera's near plane, 0.1 by default");
        REQUIRE(dispatch.job.depths.x == Approx(0.1f));
        INFO(
            "past the farthest row of lights, 34 units from the cameras, by its range of 2: " << dispatch.job.depths.y);
        REQUIRE(dispatch.job.depths.y == Approx(36.0f).margin(0.5f));
    }
}

TEST_CASE("Each view's light clusters are in a buffer of its own, of each cluster's count and its room",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    ThousandLights(test);
    const std::vector<ClusterDispatch> dispatches = Dispatches(test);
    INFO("one dispatch a view");
    REQUIRE(dispatches.size() == 2);
    const BufferHandle first = BufferAt(test, dispatches[0].groups[1], 1);
    const BufferHandle second = BufferAt(test, dispatches[1].groups[1], 1);
    INFO("a buffer each");
    REQUIRE(first != second);
    INFO("labelled for the light clusters");
    REQUIRE(RendererProbe::Label(test.Device(), first) == "Light clusters");
    INFO("each cluster's count and 256 indices, four bytes each");
    REQUIRE(RendererProbe::Bytes(test.Device(), first).size() == ClusterCount * (1 + MaxClusterLights) * 4);
    INFO("each pass clustering for its own camera");
    REQUIRE(dispatches[0].job.view != dispatches[1].job.view);
}

TEST_CASE("A view's Opaque draws bind its light clusters at set 1 binding 3, its view block saying so",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    ThousandLights(test);
    const std::vector<ClusterDispatch> dispatches = Dispatches(test);
    const std::vector<RecordedDraw> draws = test.Draws("Opaque");
    INFO("a draw of the cube in each view");
    REQUIRE(draws.size() == 2);
    for (std::size_t v = 0; v < draws.size(); ++v)
    {
        CAPTURE(v);
        INFO("the view's own cluster buffer");
        REQUIRE(BufferAt(test, draws[v].groups.at(1), 3) == BufferAt(test, dispatches.at(v).groups[1], 1));
        INFO("its view block marking the clusters there, with room for 256");
        REQUIRE(ClusterInfoOf(test, draws[v]) == std::array<std::uint32_t, 2>{1, MaxClusterLights});
    }
}

TEST_CASE("A cluster's room is the light count when there are fewer lights than its most", "[renderer][light-clusters]")
{
    RendererTestApp test;
    FewLights(test, 3);
    const std::vector<ClusterDispatch> dispatches = Dispatches(test);
    INFO("one dispatch");
    REQUIRE(dispatches.size() == 1);
    INFO("with room for the three lights");
    REQUIRE(dispatches[0].job.counts[1] == 3);
    INFO("so the buffer holds each cluster's count and three indices");
    REQUIRE(RendererProbe::Bytes(test.Device(), BufferAt(test, dispatches[0].groups[1], 1)).size() ==
            ClusterCount * 4 * 4);
    INFO("and the view block says so");
    REQUIRE(ClusterInfoOf(test, test.Draws().at(0)) == std::array<std::uint32_t, 2>{1, 3});
}

TEST_CASE("A view without lights has no LightClusters pass and loops over the empty light buffer",
          "[renderer][light-clusters]")
{
    RendererTestApp test;
    FewLights(test, 0);
    INFO("no pass");
    REQUIRE(LightClustersPasses(test) == 0);
    const RecordedDraw draw = test.Draws().at(0);
    INFO("set 1 binds the buffer of one 0");
    REQUIRE(RendererProbe::Label(test.Device(), BufferAt(test, draw.groups.at(1), 3)) == "No light clusters");
    INFO("and the view block says to loop over every light");
    REQUIRE(ClusterInfoOf(test, draw)[0] == 0);
}

TEST_CASE("With clustering off, a view loops over every light with no LightClusters pass", "[renderer][light-clusters]")
{
    RendererTestApp test;
    test.renderer.SetClusteredLights(false);
    FewLights(test, 1);
    INFO("no pass");
    REQUIRE(LightClustersPasses(test) == 0);
    const RecordedDraw draw = test.Draws().at(0);
    INFO("set 1 binds the buffer of one 0");
    REQUIRE(RendererProbe::Label(test.Device(), BufferAt(test, draw.groups.at(1), 3)) == "No light clusters");
    INFO("and the view block says to loop over every light");
    REQUIRE(ClusterInfoOf(test, draw)[0] == 0);
}

TEST_CASE("Clustering turned back on clusters from the next frame", "[renderer][light-clusters]")
{
    RendererTestApp test;
    test.renderer.SetClusteredLights(false);
    FewLights(test, 1);
    test.renderer.SetClusteredLights(true);
    test.Frame();
    INFO("the next frame has its pass again");
    REQUIRE(LightClustersPasses(test) == 1);
}
