// The sky's diffuse light on the CPU (the spec's CPU unit tests; ADR-0047): L2 spherical harmonics projected from a
// sky's cube faces, as builtin:sky-irradiance projects them, evaluated against irradiance worked analytically. A
// constant sky of radiance L gives irradiance pi L from every side; a sky of radiance 1 above the horizon and 0 below
// gives pi (1 + cos t) / 2 at a normal t from straight up, which L2 holds exactly (the step's even bands above 0
// vanish, and the cosine lobe removes its odd bands above 1). Evaluated values are the irradiance divided by pi, so a
// constant sky gives its own radiance, as a white light of intensity 1 straight onto a white face gives 1.

#include <array>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Core/SphericalHarmonics.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Vec3;
using Fenrir::Render::ConstantIrradiance;
using Fenrir::Render::EvaluateIrradiance;
using Fenrir::Render::ProjectSkyIrradiance;
using Fenrir::Render::SkyIrradiance;

namespace
{
    bool Near(Vec3 a, Vec3 b, float tolerance)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec3(tolerance)));
    }

    // Normals all round the sphere: the axes, the cube's corners, and a few in between.
    const std::array<Vec3, 12> Normals{
        Vec3(1.0f, 0.0f, 0.0f),
        Vec3(-1.0f, 0.0f, 0.0f),
        Vec3(0.0f, 1.0f, 0.0f),
        Vec3(0.0f, -1.0f, 0.0f),
        Vec3(0.0f, 0.0f, 1.0f),
        Vec3(0.0f, 0.0f, -1.0f),
        glm::normalize(Vec3(1.0f, 1.0f, 1.0f)),
        glm::normalize(Vec3(-1.0f, -1.0f, 1.0f)),
        glm::normalize(Vec3(1.0f, -1.0f, -1.0f)),
        glm::normalize(Vec3(0.3f, 0.8f, -0.5f)),
        glm::normalize(Vec3(-0.6f, 0.2f, 0.7f)),
        glm::normalize(Vec3(0.1f, -0.9f, 0.4f)),
    };
} // namespace

TEST_CASE("A constant sky projects to irradiance equal to its radiance from every side", "[renderer][sky][sh]")
{
    const Vec3 radiance(0.5f, 1.0f, 2.0f);
    const SkyIrradiance sh = ProjectSkyIrradiance([&](Vec3) { return radiance; }, 16);

    for (const Vec3 normal : Normals)
    {
        const Vec3 irradiance = EvaluateIrradiance(sh, normal);
        INFO("at (" << normal.x << ", " << normal.y << ", " << normal.z << ") the irradiance over pi is ("
                    << irradiance.x << ", " << irradiance.y << ", " << irradiance.z << "), not (0.5, 1, 2)");
        REQUIRE(Near(irradiance, radiance, 1e-3f));
    }
}

TEST_CASE("A sky lit only above the horizon projects to the analytic irradiance (1 + cos t) / 2", "[renderer][sky][sh]")
{
    const SkyIrradiance sh =
        ProjectSkyIrradiance([](Vec3 direction) { return direction.y > 0.0f ? Vec3(1.0f) : Vec3(0.0f); }, 32);

    for (const Vec3 normal : Normals)
    {
        const float expected = (1.0f + normal.y) / 2.0f;
        const Vec3 irradiance = EvaluateIrradiance(sh, normal);
        INFO("at (" << normal.x << ", " << normal.y << ", " << normal.z << ") the irradiance over pi is "
                    << irradiance.x << ", not " << expected);
        REQUIRE(Near(irradiance, Vec3(expected), 5e-3f));
    }
}

TEST_CASE("A ColourSky's coefficients give its colour as the irradiance from every side", "[renderer][sky][sh]")
{
    const Vec3 colour(0.1f, 0.2f, 0.3f);
    const SkyIrradiance sh = ConstantIrradiance(colour);

    for (const Vec3 normal : Normals)
    {
        REQUIRE(Near(EvaluateIrradiance(sh, normal), colour, 1e-6f));
    }
}
