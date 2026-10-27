// Picking, bounds and stats from C# (spec seam 7: ScreenPointToRay agreeing with C++): each answer C# gets is the one
// the Renderer owner gives C++ for the same camera, point or entity, on the null backend with no window (whose main
// window is the surface's 1280 x 720).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

#include "FenrirECS/DefaultComponents.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace RendererScriptingTests;

namespace
{
    // A camera at (2, 1, 3) turned 0.4 radians about +Y, drawing into the window's middle.
    Entity AddCamera(RendererScriptingApp& test)
    {
        Entity entity = test.Entities().CreateEntity();
        auto& transform = entity.GetComponent<Transform>();
        transform.pos = {2.0f, 1.0f, 3.0f};
        transform.rot = Math::AngleAxis(0.4f, Math::Vec3(0.0f, 1.0f, 0.0f));
        Camera camera;
        camera.viewport = {0.25f, 0.1f, 0.5f, 0.8f};
        entity.AddComponent<Camera>(camera);
        return entity;
    }
} // namespace

TEST_CASE("ScreenPointToRay and WorldToScreenPoint from C# agree with C++", "[renderer][scripting]")
{
    RendererScriptingApp test;
    const Entity camera = AddCamera(test);
    const Math::Vec2 point{500.0f, 200.0f};

    const std::optional<Math::Ray> expected = test.renderer.ScreenPointToRay(camera, point);
    REQUIRE(expected.has_value());
    {
        INFO("C# should get a ray through the point, as C++ does");
        REQUIRE(test.Call<bool>(RendererCallsType, "Ray", camera.GetId(), point.x, point.y));
    }
    {
        INFO("C#'s ray should be C++'s, origin and direction");
        REQUIRE(Exactly(test.Value(0), expected->origin.x));
        REQUIRE(Exactly(test.Value(1), expected->origin.y));
        REQUIRE(Exactly(test.Value(2), expected->origin.z));
        REQUIRE(Exactly(test.Value(3), expected->dir.x));
        REQUIRE(Exactly(test.Value(4), expected->dir.y));
        REQUIRE(Exactly(test.Value(5), expected->dir.z));
    }

    const Math::Vec3 ahead = expected->origin + expected->dir * 4.0f;
    const std::optional<Math::Vec2> screen = test.renderer.WorldToScreenPoint(camera, ahead);
    REQUIRE(screen.has_value());
    INFO("C# should see a point along the ray where C++ does");
    REQUIRE(test.Call<bool>(RendererCallsType, "ScreenPoint", camera.GetId(), ahead.x, ahead.y, ahead.z));
    REQUIRE(Exactly(test.Value(0), screen->x));
    REQUIRE(Exactly(test.Value(1), screen->y));
    INFO("A point outside the camera's viewport has no ray, as in C++");
    REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Ray", camera.GetId(), 10.0f, 10.0f));
}

TEST_CASE("CameraAt, GetWorldBounds and Stats from C# agree with C++", "[renderer][scripting]")
{
    RendererScriptingApp test;
    test.Call<void>(SceneBuilderType, "Arm");
    test.Frame();
    const auto camera = test.Call<std::uint32_t>(SceneBuilderType, "CameraEntity");
    const auto drawn = test.Call<std::uint32_t>(SceneBuilderType, "DrawnEntity");

    {
        INFO("The camera at the window's centre should be the C# system's one camera, and none outside the window");
        REQUIRE(test.Call<std::uint32_t>(RendererCallsType, "CameraAt", std::uint32_t{1}, 640.0f, 360.0f) == camera);
        REQUIRE(test.Call<std::uint32_t>(RendererCallsType, "CameraAt", std::uint32_t{1}, -5.0f, 360.0f) == noEntity);
        REQUIRE(test.Call<std::uint32_t>(RendererCallsType, "CameraAt", std::uint32_t{2}, 640.0f, 360.0f) == noEntity);
    }
    {
        const std::optional<Math::AABB> bounds = test.renderer.GetWorldBounds(test.Entities().GetEntity(drawn));
        REQUIRE(bounds.has_value());
        INFO("C#'s bounds of the drawn entity should be C++'s");
        REQUIRE(test.Call<bool>(RendererCallsType, "Bounds", drawn));
        REQUIRE(Exactly(test.Value(0), bounds->min.x));
        REQUIRE(Exactly(test.Value(1), bounds->min.y));
        REQUIRE(Exactly(test.Value(2), bounds->min.z));
        REQUIRE(Exactly(test.Value(3), bounds->max.x));
        REQUIRE(Exactly(test.Value(4), bounds->max.y));
        REQUIRE(Exactly(test.Value(5), bounds->max.z));
        INFO("The camera has no MeshRenderer, so no bounds");
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Bounds", camera));
    }
    const RendererStats stats = test.renderer.Stats();
    INFO("C#'s stats should be the last frame's, as C++'s: one draw");
    REQUIRE(stats.draws == 1);
    REQUIRE(test.Call<bool>(RendererCallsType, "Stats"));
    REQUIRE(Exactly(test.Value(0), static_cast<float>(stats.draws)));
    REQUIRE(Exactly(test.Value(1), static_cast<float>(stats.passes)));
    REQUIRE(Exactly(test.Value(2), static_cast<float>(stats.uploads)));
}
