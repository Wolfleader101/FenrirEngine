#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief The sky's diffuse light as L2 spherical harmonics (ADR-0047): nine RGB coefficients of the irradiance
     * divided by pi, in the real basis order Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22. Evaluated at a normal they
     * give what a white Lambertian face there reflects, so a constant sky gives its own radiance, as a white light of
     * intensity 1 falling straight onto a white face gives 1 (ADR-0024's plain multipliers).
     */
    using SkyIrradiance = std::array<Math::Vec3, 9>;

    /**
     * @brief The CPU mirror of builtin:sky-irradiance: the radiance `radiance(direction)` of a sky, read at the centre
     * of each of the `side` x `side` texels of every cube face (as GL and D3D lay the faces out), each weighted by its
     * solid angle, projected onto the basis and convolved with the clamped cosine. Kept step for step with the shader.
     */
    [[nodiscard]] SkyIrradiance ProjectSkyIrradiance(const std::function<Math::Vec3(Math::Vec3)>& radiance,
                                                     std::uint32_t side);

    /** @brief The coefficients of a sky of one colour (a ColourSky): `colour` as the irradiance from every side. */
    [[nodiscard]] SkyIrradiance ConstantIrradiance(Math::Vec3 colour) noexcept;

    /** @brief The irradiance divided by pi that `irradiance` gives at the unit normal `normal`. */
    [[nodiscard]] Math::Vec3 EvaluateIrradiance(const SkyIrradiance& irradiance, Math::Vec3 normal) noexcept;
} // namespace Fenrir::Render
