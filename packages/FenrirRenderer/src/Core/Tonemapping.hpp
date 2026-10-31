#pragma once

#include <cstdint>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief How a view's Output pass maps its linear HDR colour into the [0, 1] its target stores (ADR-0049): the
     * core's copy of Camera::tonemapper. The values are the numbers builtin:output switches on.
     */
    enum class Tonemapper : std::uint8_t
    {
        /** @brief Each channel clamped to [0, 1], the placeholder Output of ADR-0019. */
        None = 0,
        /** @brief AgX (Troy Sobotka's, as Blender and Filament fit it): a log curve that desaturates bright colours. */
        AgX = 1,
        /** @brief Khronos PBR Neutral: colours below 0.76 kept, only the highlights compressed. */
        PbrNeutral = 2,
        /** @brief Stephen Hill's fit of the ACES reference rendering and sRGB output transforms. */
        Aces = 3,
    };

    /** @brief What a view's colour is multiplied by before its tonemapper: 2 to the power of `exposure` EV stops. */
    [[nodiscard]] float ExposureScale(float exposure) noexcept;

    /**
     * @brief The CPU mirror of builtin:output's tonemappers: `linear` (already scaled by the exposure) mapped by
     * `tonemapper` into [0, 1], still linear; the target's sRGB encoding comes after. Kept step for step with the
     * shader, which the golden images hold to it, so a change to one is a change to both.
     */
    [[nodiscard]] Math::Vec3 Tonemap(Tonemapper tonemapper, Math::Vec3 linear) noexcept;
} // namespace Fenrir::Render
