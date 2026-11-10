#pragma once

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief A view's colour grade (ADR-0059), the core's copy of Camera::colourGrading: what the Output pass does to
     * the exposed linear Rec. 709 colour before its tonemapper. Identity by default.
     */
    struct ColourGradingSettings
    {
        /** @brief The colour temperature, in kelvin, of the light the grade takes as white. */
        float temperature = 6500.0f;
        /** @brief How far that white lies off the daylight locus, in [-1, 1]: positive greener. */
        float tint = 0.0f;
        /** @brief The slope in log2 around mid grey (0.18): 1 none. */
        float contrast = 1.0f;
        /** @brief The shadows' and the highlights' tints, linear: grey none. Their luminance is ignored. */
        Math::Vec3 shadowTint{1.0f};
        Math::Vec3 highlightTint{1.0f};
        /** @brief Where the split between the tints lies, in [-1, 1]: positive gives the highlights' more. */
        float balance = 0.0f;
        /** @brief How far each colour is from its luminance: 1 none, 0 grey. */
        float saturation = 1.0f;
        /** @brief The ASC CDL's slope, offset and power, per channel. */
        Math::Vec3 slope{1.0f};
        Math::Vec3 offset{0.0f};
        Math::Vec3 power{1.0f};
    };

    /** @brief Rec. 709 luminance: what the grade, the tints and the film grain weigh a linear colour by. */
    [[nodiscard]] float Luminance(Math::Vec3 colour) noexcept;

    /**
     * @brief `tint`, negatives clamped, scaled to unit Rec. 709 luminance, so it shifts hue and not brightness: white
     * for grey, black or a tint with no luminance. The grade's split-toning tints and the streaks' tint (ADR-0059).
     */
    [[nodiscard]] Math::Vec3 UnitLuminanceTint(Math::Vec3 tint) noexcept;

    /** @brief Whether `settings` change nothing, so Output skips the grade. */
    [[nodiscard]] bool IsIdentity(const ColourGradingSettings& settings) noexcept;

    /**
     * @brief A grade as builtin:output applies it: what of the settings is worked out once per view, on the CPU,
     * rather than per pixel.
     */
    struct ColourGrade
    {
        /** @brief Linear Rec. 709 to itself, adapted from the settings' white to the one at 6500 K and no tint. */
        Math::Mat3 whiteBalance{1.0f};
        /** @brief The slope in log2 around mid grey, at least 0. */
        float contrast = 1.0f;
        /** @brief The tints, each scaled to unit Rec. 709 luminance; white for a black tint. */
        Math::Vec3 shadowTint{1.0f};
        Math::Vec3 highlightTint{1.0f};
        /** @brief The split between the tints, in log2 stops from mid grey. */
        float split = 0.0f;
        /** @brief How far each colour is from its luminance, at least 0. */
        float saturation = 1.0f;
        /** @brief The CDL's slope per channel. */
        Math::Vec3 slope{1.0f};
        /** @brief The CDL's offset per channel. */
        Math::Vec3 offset{0.0f};
        /** @brief The CDL's power per channel, at least 0.001 (GLSL's pow(0, 0) is undefined). */
        Math::Vec3 power{1.0f};
    };

    /** @brief The grade builtin:output applies for `settings`. */
    [[nodiscard]] ColourGrade BakeColourGrade(const ColourGradingSettings& settings) noexcept;

    /**
     * @brief The CPU mirror of builtin:output's grade: `linear` (already exposed) graded, still linear, for the
     * tonemapper. Kept step for step with the shader, which the GL tests hold to it, so a change to one is a change to
     * both.
     */
    [[nodiscard]] Math::Vec3 ApplyColourGrade(const ColourGrade& grade, Math::Vec3 linear) noexcept;

    /** @brief `linear` graded by `settings`, unchanged when they are identity: ApplyColourGrade of their bake. */
    [[nodiscard]] Math::Vec3 GradeColour(const ColourGradingSettings& settings, Math::Vec3 linear) noexcept;
} // namespace Fenrir::Render
