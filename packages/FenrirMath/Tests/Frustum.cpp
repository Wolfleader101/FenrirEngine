// Math::Frustum (ADR-0057): the planes of a reversed-Z view, and the sphere and box tests the cull pass repeats. The
// expected planes are worked by hand from the projections ADR-0026 describes: a 90-degree perspective (focal length 1)
// with an infinite far plane, and an orthographic view 2 units square from 1 to 10 in front of the camera.

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "FenrirMath/Frustum.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::AABB;
using Fenrir::Math::Frustum;
using Fenrir::Math::Mat4;
using Fenrir::Math::Sphere;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;

namespace
{
    constexpr float Tolerance = 1e-5f;

    bool Near(const Vec4& a, const Vec4& b)
    {
        return std::abs(a.x - b.x) < Tolerance && std::abs(a.y - b.y) < Tolerance && std::abs(a.z - b.z) < Tolerance &&
               std::abs(a.w - b.w) < Tolerance;
    }

    /** @brief A reversed-Z perspective with an infinite far plane (ADR-0026): depth is near / -z. */
    Mat4 Perspective(float focal, float aspect, float near)
    {
        Mat4 projection(0.0f);
        projection[0][0] = focal / aspect;
        projection[1][1] = focal;
        projection[3][2] = near;
        projection[2][3] = -1.0f;
        return projection;
    }

    /** @brief A reversed-Z orthographic projection `height` tall: depth is (far + z) / (far - near). */
    Mat4 Orthographic(float height, float aspect, float near, float far)
    {
        Mat4 projection(1.0f);
        projection[0][0] = 2.0f / (height * aspect);
        projection[1][1] = 2.0f / height;
        projection[2][2] = 1.0f / (far - near);
        projection[3][2] = far / (far - near);
        return projection;
    }

    /** @brief The view of a camera at `position` looking along -Z. */
    Mat4 ViewAt(const Vec3& position)
    {
        return glm::translate(Mat4(1.0f), -position);
    }

    /** @brief The 90-degree perspective frustum from a camera at the origin: x and y within -z, z at most -1. */
    Frustum PerspectiveFrustum()
    {
        return Frustum::FromViewProjection(Perspective(1.0f, 1.0f, 1.0f));
    }

    /** @brief The orthographic frustum from a camera at the origin: x and y in [-1, 1], z in [-10, -1]. */
    Frustum OrthographicFrustum()
    {
        return Frustum::FromViewProjection(Orthographic(2.0f, 1.0f, 1.0f, 10.0f));
    }

    /** @brief A box of side 1 about `centre`. */
    AABB UnitBoxAt(const Vec3& centre)
    {
        return {centre - Vec3(0.5f), centre + Vec3(0.5f)};
    }

    /** @brief A shape's centre beyond one plane by more than half its size, and by less, for a radius of 0.5. */
    struct Straddle
    {
        const char* plane;
        Vec3 outside;
        Vec3 straddling;
    };

    constexpr float Root2 = std::numbers::sqrt2_v<float>;
} // namespace

TEST_CASE("A perspective view with an infinite far plane has five normalised planes, the far one left out",
          "[math][frustum]")
{
    const Frustum frustum = Frustum::FromViewProjection(Perspective(1.0f, 1.0f, 1.0f) * ViewAt({0.0f, 0.0f, 5.0f}));
    INFO("the far bound of an infinite projection has no normal, so the frustum has the other five planes");
    REQUIRE(frustum.Planes().size() == 5);
    // Each side plane passes through the camera at 45 degrees to its axis; the near plane is at z = 4.
    const std::array<Vec4, 5> expected{Vec4(1.0f, 0.0f, -1.0f, 5.0f) / Root2, Vec4(-1.0f, 0.0f, -1.0f, 5.0f) / Root2,
                                       Vec4(0.0f, 1.0f, -1.0f, 5.0f) / Root2, Vec4(0.0f, -1.0f, -1.0f, 5.0f) / Root2,
                                       Vec4(0.0f, 0.0f, -1.0f, 4.0f)};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const Vec4 plane = frustum.Planes()[i];
        INFO("plane " << i << " (left, right, bottom, top, near) is (" << plane.x << ", " << plane.y << ", " << plane.z
                      << ", " << plane.w << "), not (" << expected[i].x << ", " << expected[i].y << ", "
                      << expected[i].z << ", " << expected[i].w << ")");
        REQUIRE(Near(plane, expected[i]));
    }
}

TEST_CASE("An orthographic view has six planes, its far plane among them", "[math][frustum]")
{
    const Frustum frustum =
        Frustum::FromViewProjection(Orthographic(2.0f, 1.0f, 1.0f, 10.0f) * ViewAt({0.0f, 2.0f, 0.0f}));
    REQUIRE(frustum.Planes().size() == 6);
    // x in [-1, 1], y in [1, 3] about the camera at y = 2, z in [-10, -1].
    const std::array<Vec4, 6> expected{Vec4(1.0f, 0.0f, 0.0f, 1.0f),   Vec4(-1.0f, 0.0f, 0.0f, 1.0f),
                                       Vec4(0.0f, 1.0f, 0.0f, -1.0f),  Vec4(0.0f, -1.0f, 0.0f, 3.0f),
                                       Vec4(0.0f, 0.0f, -1.0f, -1.0f), Vec4(0.0f, 0.0f, 1.0f, 10.0f)};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const Vec4 plane = frustum.Planes()[i];
        INFO("plane " << i << " (left, right, bottom, top, near, far) is (" << plane.x << ", " << plane.y << ", "
                      << plane.z << ", " << plane.w << "), not (" << expected[i].x << ", " << expected[i].y << ", "
                      << expected[i].z << ", " << expected[i].w << ")");
        REQUIRE(Near(plane, expected[i]));
    }
}

TEST_CASE("A sphere is dropped only when it is wholly beyond one plane, of either kind of view", "[math][frustum]")
{
    const Frustum orthographic = OrthographicFrustum();
    INFO("a sphere in the middle of the orthographic view is kept");
    REQUIRE(orthographic.Intersects(Sphere{{0.0f, 0.0f, -5.0f}, 0.5f}));
    // Beyond each of the orthographic view's planes by 0.6 (outside) and by 0.3 (straddling), radius 0.5.
    const std::array<Straddle, 6> orthographicCases{{
        {"left", {-1.6f, 0.0f, -5.0f}, {-1.3f, 0.0f, -5.0f}},
        {"right", {1.6f, 0.0f, -5.0f}, {1.3f, 0.0f, -5.0f}},
        {"bottom", {0.0f, -1.6f, -5.0f}, {0.0f, -1.3f, -5.0f}},
        {"top", {0.0f, 1.6f, -5.0f}, {0.0f, 1.3f, -5.0f}},
        {"near", {0.0f, 0.0f, -0.4f}, {0.0f, 0.0f, -0.7f}},
        {"far", {0.0f, 0.0f, -10.6f}, {0.0f, 0.0f, -10.3f}},
    }};
    for (const Straddle& at : orthographicCases)
    {
        INFO("a sphere 0.6 beyond the orthographic " << at.plane << " plane, radius 0.5, is dropped");
        REQUIRE_FALSE(orthographic.Intersects(Sphere{at.outside, 0.5f}));
        INFO("a sphere 0.3 beyond the orthographic " << at.plane << " plane, radius 0.5, straddles it and is kept");
        REQUIRE(orthographic.Intersects(Sphere{at.straddling, 0.5f}));
    }

    const Frustum perspective = PerspectiveFrustum();
    REQUIRE(perspective.Intersects(Sphere{{0.0f, 0.0f, -5.0f}, 0.5f}));
    // At z = -5 the side planes are 5 from the axis; a centre 0.8 past one is 0.8 / sqrt 2 = 0.57 beyond it, 0.4 past
    // is 0.28 beyond it. The near plane is at z = -1.
    const std::array<Straddle, 5> perspectiveCases{{
        {"left", {-5.8f, 0.0f, -5.0f}, {-5.4f, 0.0f, -5.0f}},
        {"right", {5.8f, 0.0f, -5.0f}, {5.4f, 0.0f, -5.0f}},
        {"bottom", {0.0f, -5.8f, -5.0f}, {0.0f, -5.4f, -5.0f}},
        {"top", {0.0f, 5.8f, -5.0f}, {0.0f, 5.4f, -5.0f}},
        {"near", {0.0f, 0.0f, -0.4f}, {0.0f, 0.0f, -0.7f}},
    }};
    for (const Straddle& at : perspectiveCases)
    {
        INFO("a sphere beyond the perspective " << at.plane << " plane by more than its radius is dropped");
        REQUIRE_FALSE(perspective.Intersects(Sphere{at.outside, 0.5f}));
        INFO("a sphere straddling the perspective " << at.plane << " plane is kept");
        REQUIRE(perspective.Intersects(Sphere{at.straddling, 0.5f}));
    }
    INFO("a sphere far behind the camera, past the near plane, is dropped");
    REQUIRE_FALSE(perspective.Intersects(Sphere{{0.0f, 0.0f, 5.0f}, 0.5f}));
}

TEST_CASE("A box is dropped only when all eight corners are beyond one plane", "[math][frustum]")
{
    const Frustum orthographic = OrthographicFrustum();
    REQUIRE(orthographic.Intersects(UnitBoxAt({0.0f, 0.0f, -5.0f})));
    // Unit boxes centred 0.6 beyond a plane have every corner past it; centred 0.3 beyond, half their corners.
    const std::array<Straddle, 6> orthographicCases{{
        {"left", {-1.6f, 0.0f, -5.0f}, {-1.3f, 0.0f, -5.0f}},
        {"right", {1.6f, 0.0f, -5.0f}, {1.3f, 0.0f, -5.0f}},
        {"bottom", {0.0f, -1.6f, -5.0f}, {0.0f, -1.3f, -5.0f}},
        {"top", {0.0f, 1.6f, -5.0f}, {0.0f, 1.3f, -5.0f}},
        {"near", {0.0f, 0.0f, -0.4f}, {0.0f, 0.0f, -0.7f}},
        {"far", {0.0f, 0.0f, -10.6f}, {0.0f, 0.0f, -10.3f}},
    }};
    for (const Straddle& at : orthographicCases)
    {
        INFO("a unit box wholly beyond the orthographic " << at.plane << " plane is dropped");
        REQUIRE_FALSE(orthographic.Intersects(UnitBoxAt(at.outside)));
        INFO("a unit box straddling the orthographic " << at.plane << " plane is kept");
        REQUIRE(orthographic.Intersects(UnitBoxAt(at.straddling)));
    }

    const Frustum perspective = PerspectiveFrustum();
    REQUIRE(perspective.Intersects(UnitBoxAt({0.0f, 0.0f, -5.0f})));
    // At z = -5 a side plane is 5 from the axis, at z = -4.5 it is 4.5: a unit box centred 6.1 past the axis has its
    // nearest corner at 5.6 past, beyond the plane at every depth it spans; one centred 5.4 past has corners inside.
    const std::array<Straddle, 5> perspectiveCases{{
        {"left", {-6.1f, 0.0f, -5.0f}, {-5.4f, 0.0f, -5.0f}},
        {"right", {6.1f, 0.0f, -5.0f}, {5.4f, 0.0f, -5.0f}},
        {"bottom", {0.0f, -6.1f, -5.0f}, {0.0f, -5.4f, -5.0f}},
        {"top", {0.0f, 6.1f, -5.0f}, {0.0f, 5.4f, -5.0f}},
        {"near", {0.0f, 0.0f, -0.4f}, {0.0f, 0.0f, -0.7f}},
    }};
    for (const Straddle& at : perspectiveCases)
    {
        INFO("a unit box wholly beyond the perspective " << at.plane << " plane is dropped");
        REQUIRE_FALSE(perspective.Intersects(UnitBoxAt(at.outside)));
        INFO("a unit box straddling the perspective " << at.plane << " plane is kept");
        REQUIRE(perspective.Intersects(UnitBoxAt(at.straddling)));
    }
    INFO("a box behind the camera is dropped");
    REQUIRE_FALSE(perspective.Intersects(UnitBoxAt({0.0f, 0.0f, 5.0f})));
}

TEST_CASE("A long thin box above the view is kept by its sphere and dropped by the box test", "[math][frustum]")
{
    const Frustum frustum = OrthographicFrustum();
    // 40 long along x, 1 tall, wholly above the top plane at y = 1.
    const AABB box{{-20.0f, 1.5f, -5.5f}, {20.0f, 2.5f, -4.5f}};
    const Sphere sphere = Frustum::SphereOf(box, Mat4(1.0f));
    INFO("the sphere about the box, radius " << sphere.radius << ", reaches into the view, so it is kept");
    REQUIRE(sphere.radius > 20.0f);
    REQUIRE(frustum.Intersects(sphere));
    INFO("its eight corners are all above the top plane, so the box test drops it");
    REQUIRE_FALSE(frustum.Intersects(box));
    REQUIRE_FALSE(frustum.Intersects(box, Mat4(1.0f)));
}

TEST_CASE("The box test moves the corners by the matrix, under a non-uniform scale and a mirror", "[math][frustum]")
{
    const Frustum frustum = OrthographicFrustum();
    const AABB unit = UnitBoxAt(Vec3(0.0f));
    const Mat4 at = glm::translate(Mat4(1.0f), {2.2f, 0.0f, -5.0f});
    INFO("a unit box centred 1.2 past the right plane is dropped");
    REQUIRE_FALSE(frustum.Intersects(unit, at));
    INFO("the same box scaled 3 along x spans x from 0.7 to 3.7, back inside the plane, so it is kept");
    REQUIRE(frustum.Intersects(unit, glm::scale(at, {3.0f, 1.0f, 1.0f})));
    INFO("scaled 3 along y instead it stays beyond the right plane, so it is dropped");
    REQUIRE_FALSE(frustum.Intersects(unit, glm::scale(at, {1.0f, 3.0f, 1.0f})));

    // Mirrored in x: a box at local x 1.2 lands at world x -1.2, straddling the left plane, not the right.
    const Mat4 mirror = glm::scale(glm::translate(Mat4(1.0f), {0.0f, 0.0f, -5.0f}), {-1.0f, 1.0f, 1.0f});
    INFO("a mirrored box straddling the left plane is kept");
    REQUIRE(frustum.Intersects(UnitBoxAt({1.2f, 0.0f, 0.0f}), mirror));
    INFO("a mirrored box wholly beyond the left plane is dropped");
    REQUIRE_FALSE(frustum.Intersects(UnitBoxAt({1.8f, 0.0f, 0.0f}), mirror));
}

TEST_CASE("The sphere of a box under a matrix encloses every moved corner", "[math][frustum]")
{
    const AABB box{{-1.0f, -2.0f, -0.5f}, {3.0f, 1.0f, 2.0f}};
    const Mat4 transform = glm::scale(
        glm::rotate(glm::translate(Mat4(1.0f), {1.0f, 2.0f, 3.0f}), 0.5f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f))),
        {2.0f, 0.5f, 3.0f});
    const Sphere sphere = Frustum::SphereOf(box, transform);

    INFO("the centre is the box's centre (1, -0.5, 0.75) moved");
    const Vec3 centre(transform * Vec4(1.0f, -0.5f, 0.75f, 1.0f));
    REQUIRE(glm::length(sphere.centre - centre) < Tolerance);
    INFO("the radius is the half-diagonal, sqrt(16 + 9 + 6.25) / 2, times the largest scale, 3");
    REQUIRE(std::abs(sphere.radius - std::sqrt(31.25f) * 0.5f * 3.0f) < 1e-4f);
    for (const Vec3& corner : Fenrir::Math::Corners(box))
    {
        const Vec3 moved(transform * Vec4(corner, 1.0f));
        INFO("the moved corner (" << moved.x << ", " << moved.y << ", " << moved.z << ") is inside the sphere");
        REQUIRE(glm::length(moved - sphere.centre) <= sphere.radius + 1e-4f);
    }
}

TEST_CASE("Moved is the box about a box's corners under a matrix, and Union the box about two", "[math][frustum]")
{
    const AABB unit = UnitBoxAt(Vec3(0.0f));
    // A quarter turn about z of a unit box 3 along x: it lies along y.
    const Mat4 turned = glm::rotate(glm::scale(glm::translate(Mat4(1.0f), {1.0f, 2.0f, 3.0f}), {1.0f, 1.0f, 1.0f}),
                                    std::numbers::pi_v<float> * 0.5f, {0.0f, 0.0f, 1.0f});
    const AABB moved = Fenrir::Math::Moved({{-1.5f, -0.5f, -0.5f}, {1.5f, 0.5f, 0.5f}}, turned);
    INFO("the moved box spans y from 0.5 to 3.5 about (1, 2, 3), and x and z half a unit each way");
    REQUIRE(glm::length(moved.min - Vec3(0.5f, 0.5f, 2.5f)) < 1e-5f);
    REQUIRE(glm::length(moved.max - Vec3(1.5f, 3.5f, 3.5f)) < 1e-5f);

    const AABB both = Fenrir::Math::Union(unit, UnitBoxAt({4.0f, 0.0f, 0.0f}));
    INFO("the union of unit boxes at the origin and at x = 4 runs from -0.5 to 4.5 along x");
    REQUIRE(both.min == Vec3(-0.5f));
    REQUIRE(both.max == Vec3(4.5f, 0.5f, 0.5f));
}
