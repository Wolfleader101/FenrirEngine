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

    /** @brief The distance from a texel coordinate to the nearest whole one, as a fraction of a texel. */
    double Fraction(double texels)
    {
        return texels - std::floor(texels);
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
    const float near = 0.5f;
    const float far = 200.0f;

    SECTION("lambda 0 splits the range evenly")
    {
        const std::vector<float> splits = Fenrir::Render::CascadeSplits(near, far, 4, 0.0f);
        REQUIRE(splits.size() == 5);
        for (std::size_t i = 0; i < splits.size(); ++i)
        {
            REQUIRE(splits[i] == Approx(near + (far - near) * static_cast<float>(i) / 4.0f));
        }
    }
    SECTION("lambda 1 splits it logarithmically: each cascade as many times deeper than the one before")
    {
        const std::vector<float> splits = Fenrir::Render::CascadeSplits(near, far, 4, 1.0f);
        for (std::size_t i = 0; i < splits.size(); ++i)
        {
            REQUIRE(splits[i] == Approx(near * std::pow(far / near, static_cast<float>(i) / 4.0f)));
        }
        REQUIRE(splits[2] / splits[1] == Approx(splits[1] / splits[0]));
    }
    SECTION("between, each split is the blend of the two by lambda, rising from near to far")
    {
        const std::vector<float> even = Fenrir::Render::CascadeSplits(near, far, 3, 0.0f);
        const std::vector<float> log = Fenrir::Render::CascadeSplits(near, far, 3, 1.0f);
        const std::vector<float> splits = Fenrir::Render::CascadeSplits(near, far, 3, 0.75f);
        REQUIRE(splits.size() == 4);
        REQUIRE(splits.front() == near);
        REQUIRE(splits.back() == far);
        for (std::size_t i = 0; i < splits.size(); ++i)
        {
            REQUIRE(splits[i] == Approx(0.75f * log[i] + 0.25f * even[i]));
            if (i > 0)
            {
                REQUIRE(splits[i] > splits[i - 1]);
            }
        }
    }
}

TEST_CASE("A cascade's sphere holds its slice of the frustum and keeps its size as the camera turns",
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
            REQUIRE(glm::length(corner - Vec3(0.0f, 0.0f, -sphere.distance)) <= sphere.radius + 1e-4f);
        }
        INFO("rounded up to a sixteenth of a unit");
        REQUIRE(sphere.radius * 16.0f == Approx(std::round(sphere.radius * 16.0f)));
    }

    const CascadeConfig config;
    const std::vector<Cascade> ahead =
        Fenrir::Render::FitCascades(ViewOf({1.0f, 2.0f, 3.0f}, 0.0f), projection, Sun(), 100.0f, config);
    const std::vector<Cascade> turned =
        Fenrir::Render::FitCascades(ViewOf({1.0f, 2.0f, 3.0f}, 1.234f), projection, Sun(), 100.0f, config);
    REQUIRE(ahead.size() == 4);
    REQUIRE(turned.size() == 4);
    for (std::size_t i = 0; i < ahead.size(); ++i)
    {
        CAPTURE(i);
        INFO("turning the camera changes neither a cascade's size nor its texels'");
        REQUIRE(turned[i].radius == ahead[i].radius);
        REQUIRE(turned[i].texelSize == ahead[i].texelSize);
        INFO("a texel is the sphere's diameter over the map's side less a texel's margin on each side");
        REQUIRE(ahead[i].texelSize == Approx(2.0f * ahead[i].radius / static_cast<float>(config.mapSize - 2)));
        INFO("the cascades reach out to the shadow distance in order");
        if (i > 0)
        {
            REQUIRE(ahead[i].split > ahead[i - 1].split);
            REQUIRE(ahead[i].radius > ahead[i - 1].radius);
        }
    }
    REQUIRE(ahead.back().split == Approx(100.0f));
}

TEST_CASE("A cascade's snapped sphere does not move its texel grid as the camera moves by less than a texel",
          "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const CascadeConfig config;
    const Vec3 start{3.0f, 1.5f, -2.0f};
    const std::vector<Cascade> before = Fenrir::Render::FitCascades(ViewOf(start), projection, Sun(), 100.0f, config);
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
                const Vec3 moved = start + step * (share * texel);
                const std::vector<Cascade> after =
                    Fenrir::Render::FitCascades(ViewOf(moved), projection, Sun(), 100.0f, config);
                CAPTURE(c, step.x, step.y, step.z, share);
                INFO("the light's view moves by whole texels or not at all");
                const Vec3 shift = after[c].centre - before[c].centre;
                const Vec3 shiftInLight = Vec3(Fenrir::Render::LightRotation(Sun()) * Vec4(shift, 0.0f));
                for (const float across : {shiftInLight.x, shiftInLight.y})
                {
                    const float texels = across / texel;
                    REQUIRE(std::abs(texels - std::round(texels)) < 1e-2f);
                    REQUIRE(std::abs(texels) <= 1.0f + 1e-2f);
                }
                INFO("so each world point keeps its place within its texel");
                for (const Vec3 point : points)
                {
                    const std::array<double, 2> was = TexelOf(before[c], point, config.mapSize);
                    const std::array<double, 2> is = TexelOf(after[c], point, config.mapSize);
                    for (std::size_t axis = 0; axis < 2; ++axis)
                    {
                        const double drift = std::abs(Fraction(was.at(axis)) - Fraction(is.at(axis)));
                        REQUIRE(std::min(drift, 1.0 - drift) < 2e-2);
                    }
                }
            }
        }
    }
}

TEST_CASE("Each cascade's slice of the view lands inside its tile of the atlas, nearer the light reading deeper",
          "[renderer][shadows]")
{
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const Mat4 view = ViewOf({-4.0f, 2.0f, 1.0f}, 0.7f);
    const Mat4 cameraWorld = glm::inverse(view);
    const CascadeConfig config{3, 1024, 0.6f};
    const std::vector<Cascade> cascades = Fenrir::Render::FitCascades(view, projection, Sun(), 60.0f, config);
    REQUIRE(cascades.size() == 3);

    float start = Near;
    for (std::uint32_t c = 0; c < cascades.size(); ++c)
    {
        CAPTURE(c);
        const Mat4 atlas = Fenrir::Render::AtlasMatrix(cascades[c], c, 3);
        for (const Vec3 corner : SliceCorners(projection, start, cascades[c].split))
        {
            const Vec3 world = Vec3(cameraWorld * Vec4(corner, 1.0f));
            const Vec4 at = atlas * Vec4(world, 1.0f);
            INFO("u inside the cascade's third of the atlas, v and depth inside [0, 1]");
            REQUIRE(at.x >= static_cast<float>(c) / 3.0f);
            REQUIRE(at.x <= static_cast<float>(c + 1) / 3.0f);
            REQUIRE(at.y >= 0.0f);
            REQUIRE(at.y <= 1.0f);
            REQUIRE(at.z > 0.0f);
            REQUIRE(at.z < 1.0f);
            INFO("a point moved towards the sun reads a greater depth: reversed-Z");
            const Vec4 sunward = atlas * Vec4(world - Sun(), 1.0f);
            REQUIRE(sunward.z > at.z);
            INFO("and the sun's own direction does not move it across the map");
            REQUIRE(sunward.x == Approx(at.x).margin(1e-4));
            REQUIRE(sunward.y == Approx(at.y).margin(1e-4));
        }
        start = cascades[c].split;
    }
}

TEST_CASE("A projection's depth range reads back its near and far planes", "[renderer][shadows]")
{
    const Fenrir::Render::DepthRange perspective =
        Fenrir::Render::DepthRangeOf(Fenrir::Render::ReversedZPerspective(Fov, Aspect, 0.25f));
    REQUIRE(perspective.near == Approx(0.25f));
    REQUIRE(std::isinf(perspective.far));
    const Fenrir::Render::DepthRange orthographic =
        Fenrir::Render::DepthRangeOf(Fenrir::Render::ReversedZOrthographic(10.0f, Aspect, 0.5f, 80.0f));
    REQUIRE(orthographic.near == Approx(0.5f).margin(1e-4));
    REQUIRE(orthographic.far == Approx(80.0f).margin(1e-3));

    INFO("an orthographic camera's cascades stop at its far plane, short of the shadow distance");
    const std::vector<Cascade> cascades = Fenrir::Render::FitCascades(
        ViewOf({0.0f, 0.0f, 0.0f}), Fenrir::Render::ReversedZOrthographic(10.0f, Aspect, 0.5f, 80.0f), Sun(), 100.0f,
        CascadeConfig{});
    REQUIRE(cascades.size() == 4);
    REQUIRE(cascades.back().split == Approx(80.0f));
}
