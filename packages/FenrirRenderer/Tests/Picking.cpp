// Picking on the null backend, with no window and no GPU (ADR-0026): ScreenPointToRay, WorldToScreenPoint and
// CameraAt, computed on call from each camera's world matrix now and its target's size now. With no window the main
// window's size is the null backend's surface, 1280 x 720.

#include <cmath>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using RendererTest::RendererTestApp;

namespace
{
    constexpr float Tolerance = 1e-4f;

    bool Near(const Vec3& a, const Vec3& b, float tolerance = Tolerance)
    {
        return glm::length(a - b) <= tolerance;
    }

    bool Near(const Vec2& a, const Vec2& b, float tolerance = 1e-2f)
    {
        return glm::length(a - b) <= tolerance;
    }

    /** @brief A camera at `position` turned `yaw` radians about +Y, drawing into `viewport` at `order`. */
    Fenrir::Entity AddCamera(RendererTestApp& test, Vec3 position, float yaw = 0.0f, Fenrir::Viewport viewport = {},
                             std::int32_t order = 0)
    {
        Fenrir::Entity entity = test.AddCamera(position);
        entity.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
        entity.GetComponent<Fenrir::Camera>().viewport = viewport;
        entity.GetComponent<Fenrir::Camera>().order = order;
        return entity;
    }

    constexpr float QuarterTurn = 1.57079633f;
} // namespace

TEST_CASE("ScreenPointToRay at the viewport's centre runs from the camera along its -Z", "[renderer][picking]")
{
    RendererTestApp test;
    // Turned a quarter turn about +Y, so its -Z is world -X; drawing into the right half of the window.
    const Fenrir::Entity camera = AddCamera(test, {0.0f, 1.0f, 5.0f}, QuarterTurn, {0.5f, 0.0f, 0.5f, 1.0f});

    // The right half's centre, in the window's logical coordinates.
    const std::optional<Fenrir::Math::Ray> ray = test.renderer.ScreenPointToRay(camera, {960.0f, 360.0f});

    INFO("there is a ray, from the camera's position, along world -X");
    REQUIRE(ray.has_value());
    REQUIRE(Near(ray->origin, Vec3(0.0f, 1.0f, 5.0f)));
    REQUIRE(Near(ray->dir, Vec3(-1.0f, 0.0f, 0.0f)));
}

TEST_CASE("ScreenPointToRay through a corner follows the field of view and the viewport's aspect",
          "[renderer][picking]")
{
    RendererTestApp test;
    const Fenrir::Entity camera = AddCamera(test, {0.0f, 0.0f, 0.0f});

    // The window's top-left corner: 60 degrees vertically over a 16:9 window, so the corner's direction is
    // (-tan 30 x 16/9, tan 30, -1) before normalising.
    const std::optional<Fenrir::Math::Ray> ray = test.renderer.ScreenPointToRay(camera, {0.0f, 0.0f});

    REQUIRE(ray.has_value());
    const float t = 0.57735027f;
    INFO("the ray runs up and to the left through the corner of the frustum");
    REQUIRE(Near(ray->dir, glm::normalize(Vec3(-t * 16.0f / 9.0f, t, -1.0f))));
}

TEST_CASE("WorldToScreenPoint gives back the point a ray was made from", "[renderer][picking]")
{
    RendererTestApp test;
    const Fenrir::Entity camera = AddCamera(test, {2.0f, 1.0f, 3.0f}, 0.4f, {0.25f, 0.1f, 0.5f, 0.8f});
    const Vec2 point{500.0f, 200.0f};
    const std::optional<Fenrir::Math::Ray> ray = test.renderer.ScreenPointToRay(camera, point);
    REQUIRE(ray.has_value());

    const std::optional<Vec2> screen = test.renderer.WorldToScreenPoint(camera, ray->origin + ray->dir * 7.0f);

    INFO("a point 7 units along the ray projects back to where the ray was made");
    REQUIRE(screen.has_value());
    REQUIRE(Near(*screen, point));
}

TEST_CASE("Orthographic rays through different points are parallel, along the camera's -Z", "[renderer][picking]")
{
    RendererTestApp test;
    const Fenrir::Entity camera = AddCamera(test, {0.0f, 0.0f, 10.0f});
    camera.GetComponent<Fenrir::Camera>().projection = Fenrir::Orthographic{9.0f, 0.1f, 100.0f};

    const auto centre = test.renderer.ScreenPointToRay(camera, {640.0f, 360.0f});
    const auto corner = test.renderer.ScreenPointToRay(camera, {0.0f, 0.0f});

    REQUIRE(centre.has_value());
    REQUIRE(corner.has_value());
    INFO("both run along -Z");
    REQUIRE(Near(centre->dir, Vec3(0.0f, 0.0f, -1.0f)));
    REQUIRE(Near(corner->dir, Vec3(0.0f, 0.0f, -1.0f)));
    INFO("the corner's starts half the view's 9 x 16 units up and to the left, in the camera's plane");
    REQUIRE(Near(centre->origin, Vec3(0.0f, 0.0f, 10.0f)));
    REQUIRE(Near(corner->origin, Vec3(-8.0f, 4.5f, 10.0f)));
}

TEST_CASE("Picking a camera that draws into an image works in the image's pixels", "[renderer][picking]")
{
    RendererTestApp test;
    const auto image = test.TargetImage(200, 100);
    const Fenrir::Entity camera = AddCamera(test, {0.0f, 0.0f, 0.0f});
    camera.GetComponent<Fenrir::Camera>().target = Fenrir::ImageTarget{image};

    const auto ray = test.renderer.ScreenPointToRay(camera, {100.0f, 50.0f});
    const auto outside = test.renderer.ScreenPointToRay(camera, {300.0f, 50.0f});

    INFO("the image's centre pixel is the camera's -Z");
    REQUIRE(ray.has_value());
    REQUIRE(Near(ray->dir, Vec3(0.0f, 0.0f, -1.0f)));
    INFO("a point past the image's 200 pixels is outside");
    REQUIRE_FALSE(outside.has_value());
}

TEST_CASE("CameraAt is the highest-order enabled camera whose viewport holds the point", "[renderer][picking]")
{
    RendererTestApp test;
    const Fenrir::Entity whole = AddCamera(test, {0.0f, 0.0f, 0.0f}, 0.0f, {}, 0);
    const Fenrir::Entity left = AddCamera(test, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.5f, 1.0f}, 2);
    Fenrir::Entity disabled = AddCamera(test, {0.0f, 0.0f, 0.0f}, 0.0f, {}, 9);
    disabled.GetComponent<Fenrir::Camera>().enabled = false;
    const Fenrir::WindowId main{1};

    INFO("on the left, the order 2 camera draws over the order 0 one; the disabled order 9 one does not draw");
    REQUIRE(test.renderer.CameraAt(main, {100.0f, 100.0f}) == left);
    INFO("on the right only the whole-window camera draws");
    REQUIRE(test.renderer.CameraAt(main, {1000.0f, 100.0f}) == whole);
}

TEST_CASE("Picking gives empty for a stale entity, no Camera, a point outside or behind", "[renderer][picking]")
{
    RendererTestApp test;
    const Fenrir::Entity camera = AddCamera(test, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.5f, 0.5f});
    Fenrir::Entity gone = AddCamera(test, {0.0f, 0.0f, 0.0f});
    test.ActiveScene().GetEntityList().DestroyEntity(gone);
    const Fenrir::Entity bare = test.ActiveScene().GetEntityList().CreateEntity();

    INFO("a stale entity has no ray, no screen point");
    REQUIRE_FALSE(test.renderer.ScreenPointToRay(gone, {10.0f, 10.0f}).has_value());
    REQUIRE_FALSE(test.renderer.WorldToScreenPoint(gone, {0.0f, 0.0f, -5.0f}).has_value());
    INFO("nor does an entity with no Camera");
    REQUIRE_FALSE(test.renderer.ScreenPointToRay(bare, {10.0f, 10.0f}).has_value());
    REQUIRE_FALSE(test.renderer.WorldToScreenPoint(bare, {0.0f, 0.0f, -5.0f}).has_value());
    INFO("a point outside the top-left quarter viewport has no ray");
    REQUIRE_FALSE(test.renderer.ScreenPointToRay(camera, {1000.0f, 600.0f}).has_value());
    INFO("a point behind the camera has no screen point");
    REQUIRE_FALSE(test.renderer.WorldToScreenPoint(camera, {0.0f, 0.0f, 5.0f}).has_value());
    INFO("a point under no camera, or in a window the renderer does not draw, has no camera");
    REQUIRE_FALSE(test.renderer.CameraAt(Fenrir::WindowId{1}, {1000.0f, 600.0f}).IsValid());
    REQUIRE_FALSE(test.renderer.CameraAt(Fenrir::WindowId{2}, {10.0f, 10.0f}).IsValid());
}
