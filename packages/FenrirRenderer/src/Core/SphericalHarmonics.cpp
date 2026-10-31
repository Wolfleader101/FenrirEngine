#include "Core/SphericalHarmonics.hpp"

#include <cmath>

namespace Fenrir::Render
{
    namespace
    {
        /** @brief The real L2 basis at the unit direction `d`, in SkyIrradiance's order. */
        std::array<float, 9> Basis(Math::Vec3 d) noexcept
        {
            return {0.282095f,
                    0.488603f * d.y,
                    0.488603f * d.z,
                    0.488603f * d.x,
                    1.092548f * d.x * d.y,
                    1.092548f * d.y * d.z,
                    0.315392f * (3.0f * d.z * d.z - 1.0f),
                    1.092548f * d.x * d.z,
                    0.546274f * (d.x * d.x - d.y * d.y)};
        }

        /**
         * @brief Each coefficient's band of the clamped cosine's convolution, divided by pi (Ramamoorthi and Hanrahan
         * 2001): pi, 2 pi / 3 and pi / 4.
         */
        constexpr std::array<float, 9> CosineBands{1.0f,  2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f,
                                                   0.25f, 0.25f,       0.25f,       0.25f};

        /** @brief The world direction through the point (x, y) of cube face `face`, as builtin:panorama lays them. */
        Math::Vec3 FaceDirection(std::uint32_t face, float x, float y) noexcept
        {
            switch (face)
            {
            case 0:
                return {1.0f, y, -x};
            case 1:
                return {-1.0f, y, x};
            case 2:
                return {x, 1.0f, -y};
            case 3:
                return {x, -1.0f, y};
            case 4:
                return {x, y, 1.0f};
            default:
                return {-x, y, -1.0f};
            }
        }

        /** @brief The solid angle a face spans from its centre to the point (x, y), for texel solid angles. */
        float CornerArea(float x, float y) noexcept
        {
            return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
        }
    } // namespace

    SkyIrradiance ProjectSkyIrradiance(const std::function<Math::Vec3(Math::Vec3)>& radiance, std::uint32_t side)
    {
        SkyIrradiance sum{};
        const float texel = 2.0f / static_cast<float>(side);
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            for (std::uint32_t row = 0; row < side; ++row)
            {
                for (std::uint32_t column = 0; column < side; ++column)
                {
                    const float x0 = -1.0f + texel * static_cast<float>(column);
                    const float y0 = -1.0f + texel * static_cast<float>(row);
                    const float x1 = x0 + texel;
                    const float y1 = y0 + texel;
                    const float solidAngle =
                        CornerArea(x0, y0) - CornerArea(x0, y1) - CornerArea(x1, y0) + CornerArea(x1, y1);
                    const Math::Vec3 direction =
                        glm::normalize(FaceDirection(face, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f));
                    const Math::Vec3 weighted = radiance(direction) * solidAngle;
                    const std::array<float, 9> basis = Basis(direction);
                    for (std::size_t i = 0; i < sum.size(); ++i)
                    {
                        sum[i] += weighted * basis[i];
                    }
                }
            }
        }
        for (std::size_t i = 0; i < sum.size(); ++i)
        {
            sum[i] *= CosineBands[i];
        }
        return sum;
    }

    SkyIrradiance ConstantIrradiance(Math::Vec3 colour) noexcept
    {
        SkyIrradiance irradiance{};
        irradiance[0] = colour / Basis(Math::Vec3(0.0f, 1.0f, 0.0f))[0];
        return irradiance;
    }

    Math::Vec3 EvaluateIrradiance(const SkyIrradiance& irradiance, Math::Vec3 normal) noexcept
    {
        const std::array<float, 9> basis = Basis(normal);
        Math::Vec3 sum(0.0f);
        for (std::size_t i = 0; i < irradiance.size(); ++i)
        {
            sum += irradiance[i] * basis[i];
        }
        return sum;
    }
} // namespace Fenrir::Render
