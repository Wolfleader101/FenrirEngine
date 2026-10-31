// The BRDF every lit material shares, on the CPU (the spec's CPU unit tests; ADR-0048): height-correlated Smith
// visibility against Heitz's own form of it, white furnace tests showing a rough metal keeps the light it is lit by
// once multi-scatter compensation gives back what its single-scattering lobe loses, and specular anti-aliasing's
// widening. A white furnace is a uniform white light from every direction: whatever does not absorb light must reflect
// all of it.

#include <array>
#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "Core/Brdf.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Render::AntiAliasedRoughness;
using Fenrir::Render::BrdfScaleBias;
using Fenrir::Render::EnergyCompensation;
using Fenrir::Render::GgxDistribution;
using Fenrir::Render::SkyReflectance;
using Fenrir::Render::SkySpecularReflectance;
using Fenrir::Render::SmithVisibility;

namespace
{
    constexpr float Pi = std::numbers::pi_v<float>;

    /**
     * @brief Heitz's (2014) height-correlated masking-shadowing G2 = 1 / (1 + Lambda(v) + Lambda(l)), with
     * Lambda = (-1 + sqrt(1 + a^2 tan^2)) / 2, divided by 4 N.V N.L: the same term as SmithVisibility, written the way
     * the paper writes it rather than the way the shader does.
     */
    float HeitzVisibility(float nDotV, float nDotL, float a)
    {
        const auto lambda = [&](float cosine) {
            const float tan2 = (1.0f - cosine * cosine) / (cosine * cosine);
            return (-1.0f + std::sqrt(1.0f + a * a * tan2)) * 0.5f;
        };
        return 1.0f / (1.0f + lambda(nDotV) + lambda(nDotL)) / (4.0f * nDotV * nDotL);
    }

    /**
     * @brief What share of a white furnace a white metal's (f0 = 1) direct specular lobe D V reflects towards a viewer
     * at `nDotV`: the lobe times N.L integrated over the hemisphere on a fine grid of directions, independently of the
     * lookup table's importance sampling.
     */
    float DirectAlbedo(float nDotV, float roughness)
    {
        const float a = roughness * roughness;
        const Vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
        constexpr int thetaSteps = 512;
        constexpr int phiSteps = 1024;
        const float dTheta = 0.5f * Pi / thetaSteps;
        const float dPhi = 2.0f * Pi / phiSteps;
        double sum = 0.0;
        for (int i = 0; i < thetaSteps; ++i)
        {
            const float theta = (static_cast<float>(i) + 0.5f) * dTheta;
            for (int j = 0; j < phiSteps; ++j)
            {
                const float phi = (static_cast<float>(j) + 0.5f) * dPhi;
                const Vec3 l(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
                const Vec3 h = glm::normalize(v + l);
                const float lobe = GgxDistribution(h.z, a) * SmithVisibility(nDotV, l.z, a);
                sum += lobe * l.z * std::sin(theta) * dTheta * dPhi;
            }
        }
        return static_cast<float>(sum);
    }
} // namespace

TEST_CASE("Height-correlated Smith visibility matches Heitz's masking-shadowing over 4 N.V N.L",
          "[renderer][lighting][brdf]")
{
    INFO("at roughness 0.5 (a = 0.25), N.V 0.5 and N.L 0.8 it is 0.59322, worked from Heitz's Lambda by hand");
    REQUIRE(std::abs(SmithVisibility(0.5f, 0.8f, 0.25f) - 0.59322f) < 1e-4f);
    INFO("looking and lit straight on, nothing is masked or shadowed: 1 / 4 at any roughness");
    REQUIRE(std::abs(SmithVisibility(1.0f, 1.0f, 0.04f) - 0.25f) < 1e-5f);
    REQUIRE(std::abs(SmithVisibility(1.0f, 1.0f, 1.0f) - 0.25f) < 1e-5f);

    constexpr std::array<float, 4> cosines{0.1f, 0.35f, 0.7f, 0.95f};
    for (const float a : {0.01f, 0.25f, 0.5625f, 1.0f})
    {
        for (const float nDotV : cosines)
        {
            for (const float nDotL : cosines)
            {
                const float expected = HeitzVisibility(nDotV, nDotL, a);
                const float actual = SmithVisibility(nDotV, nDotL, a);
                INFO("at a " << a << ", N.V " << nDotV << ", N.L " << nDotL << " it is " << actual << ", not Heitz's "
                             << expected);
                REQUIRE(std::abs(actual - expected) < 1e-4f * expected + 1e-6f);
            }
        }
    }
}

TEST_CASE("A rough metal's direct specular loses energy in a white furnace, and compensation gives it back",
          "[renderer][lighting][brdf][furnace]")
{
    const Vec3 white(1.0f);
    for (const float roughness : {0.7f, 1.0f})
    {
        for (const float nDotV : {0.25f, 0.5f, 1.0f})
        {
            const float albedo = DirectAlbedo(nDotV, roughness);
            const Vec3 compensated = albedo * EnergyCompensation(white, BrdfScaleBias(nDotV, roughness));
            INFO("at roughness " << roughness << " and N.V " << nDotV << " the single-scattering lobe reflects "
                                 << albedo << " of the furnace, well short of all of it");
            REQUIRE(albedo < 0.92f);
            INFO("and compensated it reflects " << compensated.x << ", about all of it");
            REQUIRE(std::abs(compensated.x - 1.0f) < 0.02f);
        }
    }
}

TEST_CASE("A rough metal lit by a white sky reflects all of it once multiple scattering is added",
          "[renderer][lighting][brdf][furnace]")
{
    // A white sky's prefiltered cube and irradiance are both 1 everywhere, so what is reflected is the two shares.
    for (const float roughness : {0.75f, 1.0f})
    {
        const Vec2 scaleBias = BrdfScaleBias(0.5f, roughness);
        const SkyReflectance reflectance = SkySpecularReflectance(Vec3(1.0f), scaleBias);
        INFO("at roughness " << roughness << " single scattering alone reflects " << reflectance.singleScatter.x);
        REQUIRE(reflectance.singleScatter.x < 0.92f);
        const Vec3 total = reflectance.singleScatter + reflectance.multiScatter;
        INFO("with multiple scattering it reflects " << total.x << ", all of it");
        REQUIRE(std::abs(total.x - 1.0f) < 1e-3f);
    }

    INFO("a coloured metal (gold) reflects more with multiple scattering, but never more than it is lit by");
    const Vec3 gold(1.0f, 0.78f, 0.34f);
    const SkyReflectance reflectance = SkySpecularReflectance(gold, BrdfScaleBias(0.5f, 1.0f));
    const Vec3 total = reflectance.singleScatter + reflectance.multiScatter;
    for (int channel = 0; channel < 3; ++channel)
    {
        INFO("channel " << channel << ": single " << reflectance.singleScatter[channel] << ", total "
                        << total[channel]);
        REQUIRE(total[channel] > reflectance.singleScatter[channel]);
        REQUIRE(total[channel] <= 1.0f + 1e-4f);
    }
}

TEST_CASE("Specular anti-aliasing widens roughness by the normal's change across a pixel, up to a cap",
          "[renderer][lighting][brdf][anti-aliasing]")
{
    INFO("a normal that does not change across the pixel keeps its roughness");
    REQUIRE(std::abs(AntiAliasedRoughness(0.3f, Vec3(0.0f), Vec3(0.0f)) - 0.3f) < 1e-6f);

    // Variance 0.15 (0.1^2 + 0.1^2) = 0.003, doubled into a kernel of 0.006 added to a^2 = 0.3^4: (0.0141)^(1/4).
    INFO("a normal turning 0.1 a pixel each way widens roughness 0.3 to 0.34459");
    REQUIRE(std::abs(AntiAliasedRoughness(0.3f, Vec3(0.1f, 0.0f, 0.0f), Vec3(0.0f, 0.1f, 0.0f)) - 0.34459f) < 1e-4f);

    // The kernel is capped at 0.2: (0.1^4 + 0.2)^(1/4).
    INFO("however fast the normal turns, roughness 0.1 widens to no more than 0.66882");
    REQUIRE(std::abs(AntiAliasedRoughness(0.1f, Vec3(5.0f, 0.0f, 0.0f), Vec3(0.0f, 5.0f, 0.0f)) - 0.66882f) < 1e-4f);

    INFO("a fully rough surface stays fully rough");
    REQUIRE(std::abs(AntiAliasedRoughness(1.0f, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f)) - 1.0f) < 1e-6f);
}
