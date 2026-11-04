// The sun's cascades on the CPU (ADR-0050): the practical split scheme's distances, each cascade's bounding sphere,
// and the texel snapping that keeps a cascade's texel grid on the world's as the camera moves and turns.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/Cascades.hpp"
#include "Core/SceneFrame.hpp"
#include "FenrirMath/Math.hpp"

using Catch::Approx;
using Fenrir::Math::Mat4;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using Fenrir::Render::Cascade;
using Fenrir::Render::CascadeConfig;

namespace
{
    constexpr float Fov = 1.04719755f;
    constexpr float Aspect = 16.0f / 9.0f;
    constexpr float Near = 0.1f;

    /** @brief The view matrix of a camera at `position`, turned `yaw` radians about +Y, looking along its -Z. */
    Mat4 ViewOf(Vec3 position, float yaw = 0.0f)
    {
        Mat4 world = glm::mat4_cast(Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)));
        world[3] = Vec4(position, 1.0f);
        return glm::inverse(world);
    }

    /** @brief A sun from high on one side, so no world axis lines up with its texel grid. */
    Vec3 Sun()
    {
        return glm::normalize(Vec3(0.4f, -0.8f, -0.3f));
    }

    /** @brief Where `point` lands on cascade `cascade`'s texel grid, in texels from the centre of its map. */
    std::array<double, 2> TexelOf(const Cascade& cascade, Vec3 point, std::uint32_t mapSize)
    {
        const Vec4 clip = cascade.viewProjection * Vec4(point, 1.0f);
        return {static_cast<double>(clip.x) * mapSize * 0.5, static_cast<double>(clip.y) * mapSize * 0.5};
    }

    /** @brief The view-space corners of the slice of a perspective frustum between view depths `near` and `far`. */
    std::vector<Vec3> SliceCorners(const Mat4& projection, float near, float far)
    {
        const float x = 1.0f / projection[0][0];
        const float y = 1.0f / projection[1][1];
        std::vector<Vec3> corners;
        for (const float depth : {near, far})
        {
            for (const float sx : {-1.0f, 1.0f})
            {
                for (const float sy : {-1.0f, 1.0f})
                {
                    corners.emplace_back(sx * x * depth, sy * y * depth, -depth);
                }
            }
        }
        return corners;
    }
} // namespace

TEST_CASE("The practical split scheme blends logarithmic and even splits by its lambda", "[renderer][shadows]")
{
    // From 0.5 to 200 in three cascades: evenly 0.5, 67, 133.5, 200; logarithmically 0.5 x 400^(i / 3), so 0.5,
    // 3.68403, 27.14418, 200.
    const float near = 0.5f;
    const float far = 200.0f;

    SECTION("lambda 0 splits the range evenly")
    {
        const std::vector<float> splits = Fenrir::Render::CascadeSplits(near, far, 3, 0.0f);
        INFO("three cascades have four splits, at even steps");
        REQUIRE(splits.size() == 4);
        REQUIRE(splits[1] == Approx(67.0f));
        REQUIRE(splits[2] == Approx(133.5f));
    }
    SECTION("lambda 1 splits it logarithmically: each cascade as many times deeper than the one before")
    {
        const std::vector<float> splits = Fenrir::Render::CascadeSplits(near, far, 3, 1.0f);
        INFO("the splits are the near plane times 400 to the third and to two thirds");
        REQUIRE(splits.size() == 4);
        REQUIRE(splits[1] == Approx(3.68403f));
        REQUIRE(splits[2] == Approx(27.14418f));
    }
    SECTION("lambda 0.75 takes three quarters of the logarithmic split and a quarter of the even one")
    {
        const std::vector<float> splits = Fenrir::Render::CascadeSplits(near, far, 3, 0.75f);
        INFO("0.75 x 3.68403 + 0.25 x 67 and 0.75 x 27.14418 + 0.25 x 133.5, between exactly the near and far planes");
        REQUIRE(splits.size() == 4);
        REQUIRE(splits[0] == near);
        REQUIRE(splits[1] == Approx(19.51302f));
        REQUIRE(splits[2] == Approx(53.73313f));
        REQUIRE(splits[3] == far);
    }
}

TEST_CASE("A cascade's sphere holds its slice of the frustum, its radius rounded up to a sixteenth",
          "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    for (const auto& [near, far] :
         {std::array<float, 2>{0.1f, 6.0f}, std::array<float, 2>{5.0f, 30.0f}, std::array<float, 2>{25.0f, 100.0f}})
    {
        CAPTURE(near, far);
        const Fenrir::Render::CascadeSphere sphere = Fenrir::Render::FitCascadeSphere(projection, near, far);
        for (const Vec3 corner : SliceCorners(projection, near, far))
        {
            INFO("every corner of the slice is inside the sphere");
            REQUIRE(glm::length(corner - Vec3(0.0f, 0.0f, -sphere.distance)) <= sphere.radius + 1e-4f);
        }
        INFO("its radius is a whole number of sixteenths");
        REQUIRE(sphere.radius * 16.0f == Approx(std::round(sphere.radius * 16.0f)));
    }
}

TEST_CASE("Turning the camera changes neither a cascade's size nor its texels'", "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const CascadeConfig config;
    const std::vector<Cascade> ahead =
        Fenrir::Render::FitCascades(ViewOf({1.0f, 2.0f, 3.0f}, 0.0f), projection, Sun(), 100.0f, config);
    const std::vector<Cascade> turned =
        Fenrir::Render::FitCascades(ViewOf({1.0f, 2.0f, 3.0f}, 1.234f), projection, Sun(), 100.0f, config);
    INFO("four cascades either way");
    REQUIRE(ahead.size() == 4);
    REQUIRE(turned.size() == 4);
    for (std::size_t i = 0; i < ahead.size(); ++i)
    {
        CAPTURE(i);
        INFO("the same radius and texel size, to the bit");
        REQUIRE(turned[i].radius == ahead[i].radius);
        REQUIRE(turned[i].texelSize == ahead[i].texelSize);
    }
}

TEST_CASE("A cascade's texel is its sphere's diameter over its map's side less a texel's margin each side",
          "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const CascadeConfig config{4, 1026, 0.75f};
    const std::vector<Cascade> cascades =
        Fenrir::Render::FitCascades(ViewOf({0.0f, 1.0f, 0.0f}), projection, Sun(), 100.0f, config);
    INFO("four cascades");
    REQUIRE(cascades.size() == 4);
    for (const Cascade& cascade : cascades)
    {
        CAPTURE(cascade.radius);
        INFO("2 x radius over 1024 texels");
        REQUIRE(cascade.texelSize == Approx(cascade.radius / 512.0f));
    }
}

TEST_CASE("The cascades reach out in order to the shadow distance", "[renderer][shadows]")
{
    const std::vector<Cascade> cascades =
        Fenrir::Render::FitCascades(ViewOf({0.0f, 1.0f, 0.0f}), Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near),
                                    Sun(), 100.0f, CascadeConfig{});
    INFO("four cascades");
    REQUIRE(cascades.size() == 4);
    for (std::size_t i = 1; i < cascades.size(); ++i)
    {
        CAPTURE(i);
        INFO("each reaches further and is larger than the one before");
        REQUIRE(cascades[i].split > cascades[i - 1].split);
        REQUIRE(cascades[i].radius > cascades[i - 1].radius);
    }
    INFO("the last reaches the 100-unit shadow distance");
    REQUIRE(cascades.back().split == Approx(100.0f));
}

TEST_CASE("A cascade's snapped sphere does not move its texel grid as the camera moves by less than a texel",
          "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const CascadeConfig config;
    const Vec3 start{3.0f, 1.5f, -2.0f};
    const std::vector<Cascade> before = Fenrir::Render::FitCascades(ViewOf(start), projection, Sun(), 100.0f, config);
    INFO("four cascades");
    REQUIRE(before.size() == 4);
    // A world point in front of the camera, and one far off to the side: both on every cascade's grid.
    const std::array<Vec3, 2> points{Vec3(2.5f, 0.0f, -6.0f), Vec3(-40.0f, 3.0f, -70.0f)};

    for (std::size_t c = 0; c < before.size(); ++c)
    {
        const float texel = before[c].texelSize;
        // Moves of a third and of nine tenths of the cascade's texel, along each axis and diagonally.
        for (const Vec3 step : {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),
                                glm::normalize(Vec3(1.0f, -1.0f, 1.0f))})
        {
            for (const float share : {0.33f, 0.9f})
            {
                const std::vector<Cascade> after = Fenrir::Render::FitCascades(ViewOf(start + step * (share * texel)),
                                                                               projection, Sun(), 100.0f, config);
                CAPTURE(c, step.x, step.y, step.z, share);
                for (const Vec3 point : points)
                {
                    const std::array<double, 2> was = TexelOf(before[c], point, config.mapSize);
                    const std::array<double, 2> is = TexelOf(after[c], point, config.mapSize);
                    for (std::size_t axis = 0; axis < 2; ++axis)
                    {
                        const double moved = is.at(axis) - was.at(axis);
                        INFO("the point moves across the map by a whole texel or none: " << moved);
                        REQUIRE(std::abs(moved - std::round(moved)) < 2e-2);
                        REQUIRE(std::abs(moved) <= 1.0 + 2e-2);
                    }
                }
            }
        }
    }
}

TEST_CASE("Each cascade's slice of the view lands inside its tile of the atlas", "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const Mat4 view = ViewOf({-4.0f, 2.0f, 1.0f}, 0.7f);
    const Mat4 cameraWorld = glm::inverse(view);
    const CascadeConfig config{3, 1024, 0.6f};
    const std::vector<Cascade> cascades = Fenrir::Render::FitCascades(view, projection, Sun(), 60.0f, config);
    INFO("three cascades");
    REQUIRE(cascades.size() == 3);

    float start = Near;
    for (std::uint32_t c = 0; c < cascades.size(); ++c)
    {
        CAPTURE(c);
        const Mat4 atlas = Fenrir::Render::AtlasMatrix(cascades[c], c, 3);
        for (const Vec3 corner : SliceCorners(projection, start, cascades[c].split))
        {
            const Vec4 at = atlas * Vec4(Vec3(cameraWorld * Vec4(corner, 1.0f)), 1.0f);
            INFO("u inside the cascade's third of the atlas, v and depth inside [0, 1]");
            REQUIRE(at.x >= static_cast<float>(c) / 3.0f);
            REQUIRE(at.x <= static_cast<float>(c + 1) / 3.0f);
            REQUIRE(at.y >= 0.0f);
            REQUIRE(at.y <= 1.0f);
            REQUIRE(at.z > 0.0f);
            REQUIRE(at.z < 1.0f);
        }
        start = cascades[c].split;
    }
}

TEST_CASE("A point moved towards the sun reads a greater depth in the atlas at the same texel", "[renderer][shadows]")
{
    const std::vector<Cascade> cascades =
        Fenrir::Render::FitCascades(ViewOf({0.0f, 1.0f, 0.0f}), Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near),
                                    Sun(), 60.0f, CascadeConfig{});
    INFO("four cascades");
    REQUIRE(cascades.size() == 4);
    const Mat4 atlas = Fenrir::Render::AtlasMatrix(cascades[0], 0, 4);
    const Vec3 point{0.2f, 0.0f, -1.5f};
    const Vec4 at = atlas * Vec4(point, 1.0f);
    const Vec4 sunward = atlas * Vec4(point - Sun() * 0.5f, 1.0f);
    INFO("nearer the sun is deeper: reversed-Z");
    REQUIRE(sunward.z > at.z);
    INFO("and the sun's own direction does not move it across the map");
    REQUIRE(sunward.x == Approx(at.x).margin(1e-4));
    REQUIRE(sunward.y == Approx(at.y).margin(1e-4));
}

TEST_CASE("A projection's depth range reads back its near and far planes", "[renderer][shadows]")
{
    const Fenrir::Render::DepthRange perspective =
        Fenrir::Render::DepthRangeOf(Fenrir::Render::ReversedZPerspective(Fov, Aspect, 0.25f));
    INFO("a perspective's near plane, and no far one");
    REQUIRE(perspective.near == Approx(0.25f));
    REQUIRE(std::isinf(perspective.far));
    const Fenrir::Render::DepthRange orthographic =
        Fenrir::Render::DepthRangeOf(Fenrir::Render::ReversedZOrthographic(10.0f, Aspect, 0.5f, 80.0f));
    INFO("an orthographic projection's near and far planes");
    REQUIRE(orthographic.near == Approx(0.5f).margin(1e-4));
    REQUIRE(orthographic.far == Approx(80.0f).margin(1e-3));
}

TEST_CASE("An orthographic camera's cascades stop at its far plane, short of the shadow distance",
          "[renderer][shadows]")
{
    const std::vector<Cascade> cascades = Fenrir::Render::FitCascades(
        ViewOf({0.0f, 0.0f, 0.0f}), Fenrir::Render::ReversedZOrthographic(10.0f, Aspect, 0.5f, 80.0f), Sun(), 100.0f,
        CascadeConfig{});
    INFO("four cascades, the last reaching 80, not 100");
    REQUIRE(cascades.size() == 4);
    REQUIRE(cascades.back().split == Approx(80.0f));
}
