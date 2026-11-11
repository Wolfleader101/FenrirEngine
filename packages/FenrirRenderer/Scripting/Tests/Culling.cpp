// Culling from C# (the culling spec's ticket 09, ADR-0057): the switches and thresholds C# sets reach the cull passes
// from the next frame, the frustum C# reads is the one C++ computes, plane for plane, and crosses back unchanged, its
// hand-written tests agree with Math::Frustum's, and every RendererStats count, the culling counts included, crosses in
// its place.

#include <catch2/catch_test_macros.hpp>

#include <Iris/FieldDifferences.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CullProbe.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Frustum.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirRendererScripting/Generated/FenrirRendererMappings.hpp"
#include "FenrirScripting/Generated/FenrirMathMappings.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* CullingCallsType = "RendererScriptFixture.CullingCalls";
    constexpr const char* RoundTripCallsType = "RendererScriptFixture.RoundTripCalls";

    // The fields that differ between two values, one per line.
    template <typename T>
    std::string Differences(const T& expected, const T& actual)
    {
        std::string text;
        for (const std::string& difference : Iris::FieldDifferences(expected, actual, {}))
        {
            text += "\n  " + difference;
        }
        return text;
    }

    // A drawn mesh at (1, 2, 3) and a camera at (1, 2, 5) looking at it (SceneBuilder), drawn for two frames so the
    // camera's cull passes have run; the camera's entity.
    Entity BuildScene(RendererScriptingApp& test)
    {
        test.Call<void>(SceneBuilderType, "Arm");
        test.ApplyChanges();
        test.Frame();
        test.Frame();
        const std::uint32_t camera = test.Call<std::uint32_t>(SceneBuilderType, "CameraEntity");
        INFO("The scene builder should have made its camera");
        REQUIRE(test.Entities().HasEntity(camera));
        return test.Entities().GetEntity(camera);
    }

    // The job block of the first Cull pass of the last frame: its bytes and where the block starts in them.
    struct JobBlock
    {
        std::vector<std::byte> bytes;
        std::size_t offset = 0;
    };

    JobBlock FirstJob(RendererScriptingApp& test)
    {
        const std::vector<RendererTest::RecordedCull> culls = RendererTest::CullsIn(test.Submitted());
        INFO("The last frame should have run a Cull pass for the camera");
        REQUIRE_FALSE(culls.empty());
        const Graphics::BindingGroupEntry job = RendererTest::EntryAt(test.Device(), culls.front().job, 0);
        return {RendererProbe::Bytes(test.Device(), job.buffer), job.offset};
    }

    // How many planes the first Cull pass tests against: the job block's groups.z (0 with GPU culling off).
    std::uint32_t JobPlaneCount(RendererScriptingApp& test)
    {
        const JobBlock job = FirstJob(test);
        return RendererTest::UintAt(job.bytes, job.offset + 8);
    }

    // The job block's two thresholds: x and y of its `lod` vector, at offset 128.
    std::pair<float, float> JobLod(RendererScriptingApp& test)
    {
        const JobBlock job = FirstJob(test);
        return {FloatAt(job.bytes, job.offset + 128), FloatAt(job.bytes, job.offset + 132)};
    }

    // How many Cull passes the last frame ran: two a camera with occlusion culling, one without.
    std::size_t CullPasses(RendererScriptingApp& test)
    {
        return RendererTest::CullsIn(test.Submitted()).size();
    }

    // Whether two frustums hold the same planes and count, bit for bit.
    bool SameFrustum(const Math::Frustum& expected, const Math::Frustum& actual)
    {
        return Iris::FieldDifferences(expected, actual, {}).empty();
    }
} // namespace

TEST_CASE("The GPU culling switch C# sets reaches the cull pass from the next frame", "[renderer][scripting][culling]")
{
    RendererScriptingApp test;
    BuildScene(test);
    {
        INFO("On, the camera's Cull pass tests against its five planes");
        REQUIRE(JobPlaneCount(test) == 5);
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "GpuCulling", false));
    test.Frame();
    {
        INFO("Off from C#, the pass is given no planes, so it keeps every candidate");
        REQUIRE(JobPlaneCount(test) == 0);
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "GpuCulling", true));
    test.Frame();
    INFO("On again, the planes are back");
    REQUIRE(JobPlaneCount(test) == 5);
}

TEST_CASE("The occlusion culling switch C# sets decides whether a camera culls in two phases",
          "[renderer][scripting][culling]")
{
    RendererScriptingApp test;
    BuildScene(test);
    {
        INFO("With occlusion culling, the default, the camera runs two Cull passes a frame");
        REQUIRE(CullPasses(test) == 2);
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "OcclusionCulling", false));
    test.Frame();
    {
        INFO("Off from C#, it runs one");
        REQUIRE(CullPasses(test) == 1);
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "OcclusionCulling", true));
    test.Frame();
    INFO("On again, two");
    REQUIRE(CullPasses(test) == 2);
}

TEST_CASE("Freezing culling from C# keeps the camera's frustum until it is thawed", "[renderer][scripting][culling]")
{
    RendererScriptingApp test;
    const Entity camera = BuildScene(test);
    {
        INFO("Nothing is frozen to begin with");
        REQUIRE(RendererProbe::FrozenCulls(test.renderer).empty());
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "CullingFrozen", true));
    test.Frame();
    {
        const std::vector<Render::FrozenCull>& frozen = RendererProbe::FrozenCulls(test.renderer);
        INFO("Frozen from C#, the camera's frustum is kept under its entity");
        REQUIRE(frozen.size() == 1);
        REQUIRE(frozen.front().camera == camera.GetId());
        REQUIRE(frozen.front().frustum.Planes().size() == 5);
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "CullingFrozen", false));
    test.Frame();
    INFO("Thawed from C#, it is forgotten");
    REQUIRE(RendererProbe::FrozenCulls(test.renderer).empty());
}

TEST_CASE("The level-of-detail thresholds C# sets reach the job block from the next frame",
          "[renderer][scripting][culling][lod]")
{
    RendererScriptingApp test;
    BuildScene(test);
    {
        const auto [error, minimum] = JobLod(test);
        INFO("The defaults are RendererConfig's: an error of a pixel, nothing dropped for size");
        REQUIRE(Exactly(error, 1.0f));
        REQUIRE(Exactly(minimum, 0.0f));
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "LodSelection", 2.5f, 3.0f));
    test.Frame();
    {
        const auto [error, minimum] = JobLod(test);
        INFO("Both thresholds C# set are in the job block");
        REQUIRE(Exactly(error, 2.5f));
        REQUIRE(Exactly(minimum, 3.0f));
    }
    REQUIRE(test.Call<bool>(CullingCallsType, "LodSelection", 1.0f, 0.0f));
    test.Frame();
    const auto [error, minimum] = JobLod(test);
    INFO("The defaults set again from C# are back in the job block");
    REQUIRE(Exactly(error, 1.0f));
    REQUIRE(Exactly(minimum, 0.0f));
}

TEST_CASE("The frustum C# reads is the camera's, plane for plane, and crosses back unchanged",
          "[renderer][scripting][culling]")
{
    RendererScriptingApp test;
    const Entity camera = BuildScene(test);
    const std::optional<Math::Frustum> expected = Renderer::GetFrustum(test.Get(), camera);
    {
        INFO("C++ should have the perspective camera's five-plane frustum to compare with");
        REQUIRE(expected.has_value());
        REQUIRE(expected->Planes().size() == 5);
    }
    {
        INFO("C# finds the camera's frustum");
        REQUIRE(test.Call<bool>(CullingCallsType, "HasFrustum", camera.GetId()));
    }
    const Math::Frustum read = test.Call<Math::Frustum>(CullingCallsType, "GetFrustum", camera.GetId());
    {
        INFO("The frustum C# read differs from C++'s:" << Differences(*expected, read));
        REQUIRE(SameFrustum(*expected, read));
        REQUIRE(test.Call<int>(CullingCallsType, "PlaneCount", *expected) == 5);
    }
    {
        const Math::Frustum echoed = test.Call<Math::Frustum>(CullingCallsType, "EchoFrustum", *expected);
        INFO("The frustum C++ passed C# comes back with these planes changed:" << Differences(*expected, echoed));
        REQUIRE(Iris::FieldDifferences(*expected, echoed, {}).empty());
    }
    {
        // An orthographic camera has a far plane, so six.
        camera.GetComponent<Camera>().projection = Orthographic{4.0f, 0.1f, 50.0f};
        test.Frame();
        const Math::Frustum orthographic = test.Call<Math::Frustum>(CullingCallsType, "GetFrustum", camera.GetId());
        INFO("An orthographic camera's frustum has six planes in C# too");
        REQUIRE(orthographic.Planes().size() == 6);
        REQUIRE(SameFrustum(*Renderer::GetFrustum(test.Get(), camera), orthographic));
    }
    const Entity plain = test.Entities().CreateEntity();
    {
        INFO("An entity with no Camera has no frustum, and C# gets one with no planes");
        REQUIRE_FALSE(test.Call<bool>(CullingCallsType, "HasFrustum", plain.GetId()));
        REQUIRE(test.Call<Math::Frustum>(CullingCallsType, "GetFrustum", plain.GetId()).Planes().empty());
    }
}

TEST_CASE("C#'s Frustum tests agree with Math::Frustum's for boxes, moved boxes and spheres",
          "[renderer][scripting][culling]")
{
    RendererScriptingApp test;
    const Entity camera = BuildScene(test);
    const Math::Frustum frustum = *Renderer::GetFrustum(test.Get(), camera);
    // The drawn mesh's place, a box behind the camera, one far to the right, one around everything, a thin one along
    // the view's edge, and a box with no planes to fail.
    const std::array<Math::AABB, 6> boxes = {Math::AABB{{0.5f, 1.5f, 2.5f}, {1.5f, 2.5f, 3.5f}},
                                             Math::AABB{{0.0f, 1.0f, 7.0f}, {2.0f, 3.0f, 9.0f}},
                                             Math::AABB{{40.0f, 1.0f, 2.0f}, {42.0f, 3.0f, 4.0f}},
                                             Math::AABB{{-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f}},
                                             Math::AABB{{-3.0f, 1.9f, 3.9f}, {-2.0f, 2.1f, 4.1f}},
                                             Math::AABB{{1.0f, 2.0f, 4.9f}, {1.0f, 2.0f, 4.9f}}};
    bool anyIn = false;
    bool anyOut = false;
    for (const Math::AABB& box : boxes)
    {
        CAPTURE(box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z);
        const bool expected = frustum.Intersects(box);
        anyIn = anyIn || expected;
        anyOut = anyOut || !expected;
        REQUIRE(test.Call<bool>(CullingCallsType, "IntersectsBox", frustum, box) == expected);
    }
    {
        INFO("The boxes should cover both answers, or the comparison proves little");
        REQUIRE(anyIn);
        REQUIRE(anyOut);
    }
    // The mesh's box moved out of view, into it, and scaled over it.
    const std::array<Math::Mat4, 3> transforms = {
        Math::Translate(Math::Mat4(1.0f), Math::Vec3(50.0f, 0.0f, 0.0f)),
        Math::Translate(Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -2.0f)),
        Math::Scale(Math::Mat4(1.0f), Math::Vec3(300.0f)),
    };
    for (const Math::Mat4& transform : transforms)
    {
        CAPTURE(transform[3].x, transform[3].z, transform[0].x);
        const bool expected = frustum.Intersects(boxes[0], transform);
        REQUIRE(test.Call<bool>(CullingCallsType, "IntersectsMoved", frustum, boxes[0], transform) == expected);
    }
    const std::array<Math::Sphere, 4> spheres = {
        Math::Sphere{{1.0f, 2.0f, 3.0f}, 0.5f}, Math::Sphere{{1.0f, 2.0f, 8.0f}, 0.5f},
        Math::Sphere{{1.0f, 2.0f, 8.0f}, 10.0f}, Math::Sphere{{30.0f, 2.0f, 3.0f}, 1.0f}};
    for (const Math::Sphere& sphere : spheres)
    {
        CAPTURE(sphere.centre.x, sphere.centre.z, sphere.radius);
        const bool expected = frustum.Intersects(sphere);
        REQUIRE(test.Call<bool>(CullingCallsType, "IntersectsSphere", frustum, sphere.centre, sphere.radius) ==
                expected);
    }
    INFO("A new Frustum has no planes, so everything is inside it, as in C++");
    REQUIRE(test.Call<bool>(CullingCallsType, "IntersectsBox", Math::Frustum{}, boxes[1]));
}

TEST_CASE("Every RendererStats count, the culling counts included, crosses between C++ and C# in its place",
          "[renderer][scripting][culling]")
{
    // A frame on the null backend leaves the read-back and skinning counts zero, so each count is given a value of
    // its own here, the arrays' entries included.
    RendererScriptingApp test;
    RendererStats stats;
    stats.draws = 1;
    stats.instances = 2;
    stats.instancesDrawn = 3;
    for (std::uint32_t level = 0; level < 8; ++level)
    {
        stats.drawnByLod[level] = 4 + level;
    }
    stats.drawnByPhase[0] = 12;
    stats.drawnByPhase[1] = 13;
    stats.passes = 14;
    stats.uploads = 15;
    stats.paletteBytesUploaded = 16;
    stats.distinctPalettesWritten = 17;
    stats.skinnedDraws = 18;
    stats.shadowDraws = 19;
    const RendererStats echoed = test.Call<RendererStats>(RoundTripCallsType, "EchoStats", stats);
    INFO("RendererStats C++ passed C# come back with these counts changed:" << Differences(stats, echoed));
    REQUIRE(Iris::FieldDifferences(stats, echoed, {}).empty());
    INFO("C# reads instancesDrawn where C++ keeps it: zero on the null backend, which runs no cull shader");
    BuildScene(test);
    REQUIRE(test.Call<std::uint32_t>(CullingCallsType, "InstancesDrawn") == Renderer::Stats(test.Get()).instancesDrawn);
}
