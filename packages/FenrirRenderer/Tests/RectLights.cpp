// Rect lights' linearly transformed cosines on the CPU (the spec's CPU unit tests; ADR-0048): Core/RectLights mirrors
// Fenrir/Lighting.glsl step for step, so these check the maths every lit material shares. The diffuse lobe's LTC
// integral of a rect is its form factor, which a brute-force sum over the rect's area gives too.

#include <algorithm>
#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "Core/RectLights.hpp"

using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Render::GpuLight;
using Fenrir::Render::LightKind;
using Fenrir::Render::RectLight;

namespace
{
    /** @brief A rect light centred at `position`, its width along `right` and height along `up`, of radiance 1. */
    GpuLight Rect(Vec3 position, Vec3 right, Vec3 up, float width, float height, bool twoSided = false)
    {
        RectLight light;
        light.position = position;
        light.right = glm::normalize(right);
        light.up = glm::normalize(up);
        light.colour = Vec3(1.0f);
        light.width = width;
        light.height = height;
        light.twoSided = twoSided;
        light.range = 100.0f;
        return Fenrir::Render::ToGpuLight(light);
    }

    /**
     * @brief The form factor of `light` from `point` facing `normal`, summed over a `cells` x `cells` grid of its area:
     * the sum of cos(at the surface) x cos(at the light) / (pi r^2) dA, the surface's cosine clamped at its horizon and
     * the light's at its back unless it is two-sided.
     */
    double NumericalDiffuse(Vec3 point, Vec3 normal, const GpuLight& light, int cells = 400)
    {
        const double cellArea = 4.0 * glm::length(light.halfWidth) * glm::length(light.halfHeight) / (cells * cells);
        double sum = 0.0;
        for (int i = 0; i < cells; ++i)
        {
            for (int j = 0; j < cells; ++j)
            {
                const float u = -1.0f + (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(cells);
                const float v = -1.0f + (2.0f * static_cast<float>(j) + 1.0f) / static_cast<float>(cells);
                const Vec3 toCell = light.position + light.halfWidth * u + light.halfHeight * v - point;
                const double distanceSquared = glm::dot(toCell, toCell);
                const Vec3 w = glm::normalize(toCell);
                const double atSurface = glm::dot(w, normal);
                double atLight = glm::dot(-w, light.direction);
                atLight = light.twoSided != 0 ? std::abs(atLight) : std::max(atLight, 0.0);
                if (atSurface > 0.0)
                {
                    sum += atSurface * atLight / distanceSquared;
                }
            }
        }
        return sum * cellArea / std::numbers::pi;
    }

    bool NearRelative(double a, double b, double tolerance)
    {
        return std::abs(a - b) <= tolerance * std::abs(b);
    }
} // namespace

TEST_CASE("A rect light's record faces its entity's -Z and holds its half extents", "[renderer][lights][rect]")
{
    const GpuLight gpu = Rect({1.0f, 2.0f, 3.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, 2.0f, true);
    REQUIRE(gpu.kind == static_cast<std::uint32_t>(LightKind::Rect));
    REQUIRE(gpu.position == Vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(glm::length(gpu.direction - Vec3(0.0f, 0.0f, -1.0f)) < 1e-6f);
    REQUIRE(gpu.halfWidth == Vec3(2.0f, 0.0f, 0.0f));
    REQUIRE(gpu.halfHeight == Vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(gpu.twoSided == 1u);
    REQUIRE(gpu.range == 100.0f);
}

TEST_CASE("The LTC tables are 64 x 64 RGBA texels, sampled at a texel's centre as that texel",
          "[renderer][lights][rect]")
{
    using Fenrir::Render::LtcSide;
    REQUIRE(Fenrir::Render::LtcMatrixTexels().size() == std::size_t{LtcSide} * LtcSide * 4);
    REQUIRE(Fenrir::Render::LtcAmplitudeTexels().size() == std::size_t{LtcSide} * LtcSide * 4);

    INFO("seen straight on, GGX is round about the normal: the transform only squashes it along the normal, the more "
         "the smoother");
    const auto matrix = [](float roughness) {
        return Fenrir::Render::SampleLtcTable(Fenrir::Render::LtcMatrixTexels(),
                                              Fenrir::Render::LtcUv({roughness, 0.0f}));
    };
    for (const float roughness : {0.25f, 0.5f, 1.0f})
    {
        const auto m = matrix(roughness);
        REQUIRE(m.x == 1.0f);
        REQUIRE(m.y == 0.0f);
        REQUIRE(m.z == 0.0f);
    }
    REQUIRE(matrix(0.25f).w < matrix(0.5f).w);
    REQUIRE(matrix(0.5f).w < matrix(1.0f).w);

    INFO("a sphere straight overhead is not clipped by the horizon at all");
    const auto overhead =
        Fenrir::Render::SampleLtcTable(Fenrir::Render::LtcAmplitudeTexels(), Fenrir::Render::LtcUv({1.0f, 0.5f}));
    REQUIRE(std::abs(overhead.w - 1.0f) < 0.01f);
}

TEST_CASE("The LTC diffuse integral of a rect light matches numerical integration", "[renderer][lights][rect]")
{
    const Vec3 point(0.0f);
    const Vec3 up(0.0f, 1.0f, 0.0f);
    struct Case
    {
        const char* name;
        Vec3 normal;
        GpuLight light;
    };
    // Each light faces the point: its -(right x up) points towards it.
    const Case cases[] = {
        {"2 x 1 overhead, facing straight down", up,
         Rect({0.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 2.0f, 1.0f)},
        {"overhead, off to one side and tilted 40 degrees", up,
         Rect({0.5f, 1.2f, 0.3f}, {std::cos(0.7f), -std::sin(0.7f), 0.0f}, {0.0f, 0.0f, -1.0f}, 2.0f, 1.2f)},
        {"on a wall to the side, seen at a grazing angle", up,
         Rect({1.5f, 0.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, 1.2f, 1.2f)},
        {"overhead, over a tilted surface", glm::normalize(Vec3(0.4f, 1.0f, 0.2f)),
         Rect({0.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 2.0f, 1.0f)},
        {"a large panel close by", up, Rect({0.3f, 0.5f, -0.2f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 6.0f, 4.0f)},
    };
    for (const Case& c : cases)
    {
        const double ltc = Fenrir::Render::RectDiffuse(point, c.normal, c.light);
        const double numerical = NumericalDiffuse(point, c.normal, c.light);
        INFO(c.name << ": LTC " << ltc << ", numerical " << numerical);
        REQUIRE(numerical > 0.01);
        // Above the horizon the integral is exact but for Hill's fit of each edge and the half-float tables.
        REQUIRE(NearRelative(ltc, numerical, 2e-3));
    }
}

TEST_CASE("A rect light straddling a surface's horizon lights it within the sphere approximation's error",
          "[renderer][lights][rect]")
{
    // On a wall beside the point, half above its horizon and half below: the clipless form clips the horizon with a
    // sphere of the same vector form factor, not the rect itself, which here gives about 12% too much (0.002 of 0.019).
    const GpuLight light = Rect({2.0f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, 1.2f, 1.2f);
    const Vec3 normal(0.0f, 1.0f, 0.0f);
    const double ltc = Fenrir::Render::RectDiffuse(Vec3(0.0f), normal, light);
    const double numerical = NumericalDiffuse(Vec3(0.0f), normal, light);
    INFO("LTC " << ltc << ", numerical " << numerical);
    REQUIRE(std::abs(ltc - numerical) < 0.005);

    INFO("and wholly below the horizon, nothing");
    const GpuLight below = Rect({2.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, 1.2f, 1.2f);
    REQUIRE(Fenrir::Render::RectDiffuse(Vec3(0.0f), normal, below) < 1e-3f);
}

TEST_CASE("A one-sided rect light lights nothing behind it; a two-sided one lights both sides alike",
          "[renderer][lights][rect]")
{
    const Vec3 up(0.0f, 1.0f, 0.0f);
    // Overhead, facing up, away from the point below it.
    const GpuLight away = Rect({0.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 2.0f, 1.0f);
    REQUIRE(Fenrir::Render::RectDiffuse(Vec3(0.0f), up, away) == 0.0f);

    GpuLight twoSided = away;
    twoSided.twoSided = 1;
    GpuLight facing = Rect({0.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 2.0f, 1.0f);
    const float front = Fenrir::Render::RectDiffuse(Vec3(0.0f), up, facing);
    INFO("the front's light is " << front);
    REQUIRE(front > 0.1f);
    REQUIRE(std::abs(Fenrir::Render::RectDiffuse(Vec3(0.0f), up, twoSided) - front) < 1e-4f);
}

TEST_CASE("A rect light's window is 1 near it and falls to 0 at its range from the rect's nearest point",
          "[renderer][lights][rect]")
{
    GpuLight light = Rect(Vec3(0.0f), {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, 2.0f);
    light.range = 3.0f;
    INFO("in front of its centre, a tenth of the range away: (1 - 0.1^4)^2");
    REQUIRE(std::abs(Fenrir::Render::RectWindow({0.0f, 0.0f, -0.3f}, light) - 0.9998f) < 1e-4f);
    INFO("beside its right edge, measured from the edge, not the centre");
    REQUIRE(Fenrir::Render::RectWindow({4.9f, 0.0f, 0.0f}, light) > 0.0f);
    REQUIRE(Fenrir::Render::RectWindow({5.0f, 0.0f, 0.0f}, light) == 0.0f);
    INFO("half the range out: (1 - (1/2)^4)^2");
    REQUIRE(std::abs(Fenrir::Render::RectWindow({0.0f, 2.5f, 0.0f}, light) - 0.87890625f) < 1e-5f);
}
