// The CPU mirror of builtin:output's colour grade (ADR-0059): kept step for step with Shaders/Builtin/output.frag,
// which the GL tests hold to it, so a change to one is a change to both. The white balance's matrix is worked out
// here, once per view, in double precision; the shader only applies it.
#include "Core/ColourGrading.hpp"

#include <algorithm>
#include <cmath>

namespace Fenrir::Render
{
    namespace
    {
        using Math::Vec3;

        /** @brief Mid grey, which contrast and the split between the tints pivot on. */
        constexpr float MidGrey = 0.18f;
        /** @brief How many stops either side of the split the shadow tint crosses into the highlight tint over. */
        constexpr float SplitWidth = 4.0f;
        /** @brief How many stops from mid grey a balance of 1 moves the split. */
        constexpr float BalanceStops = 4.0f;

        /** @brief Rec. 709 luminance. */
        float Luminance(Vec3 colour)
        {
            return glm::dot(colour, Vec3(0.2126f, 0.7152f, 0.0722f));
        }

        /** @brief Whether `tint` tints nothing: grey (any luminance), or black. */
        bool IsGrey(Vec3 tint)
        {
            return tint.r == tint.g && tint.g == tint.b;
        }

        /**
         * @brief `tint`, negatives clamped, scaled to unit luminance, so it shifts hue and not brightness: white for
         * grey, black or a tint with no luminance.
         */
        Vec3 Normalised(Vec3 tint)
        {
            tint = glm::max(tint, Vec3(0.0f));
            const float luminance = Luminance(tint);
            return IsGrey(tint) || luminance <= 0.0f ? Vec3(1.0f) : tint / luminance;
        }

        /** @brief A matrix written row by row, as the papers print them. */
        glm::dmat3 Rows(glm::dvec3 r0, glm::dvec3 r1, glm::dvec3 r2)
        {
            return glm::transpose(glm::dmat3(r0, r1, r2));
        }

        /** @brief Linear Rec. 709 to CIE XYZ, white D65 (IEC 61966-2-1, as Lindbloom works it to 7 places). */
        glm::dmat3 Rec709ToXyz()
        {
            return Rows({0.4124564, 0.3575761, 0.1804375}, {0.2126729, 0.7151522, 0.0721750},
                        {0.0193339, 0.1191920, 0.9503041});
        }

        /** @brief CIECAM02's chromatic adaptation transform, CIE XYZ to its sharpened LMS cone space. */
        glm::dmat3 Cat02()
        {
            return Rows({0.7328, 0.4296, -0.1624}, {-0.7036, 1.6975, 0.0061}, {0.0030, 0.0136, 0.9834});
        }

        /**
         * @brief The CIE xy of daylight at `kelvin` (CIE 15's daylight locus), the temperature first scaled by
         * 1.4388 / 1.438 as CIE 15 defines the D illuminants after the second radiation constant changed, so 6500
         * gives D65. Below 4000 K the formula is extrapolated; it stays within 0.015 in x of the Planckian locus down
         * to 2500 K.
         */
        glm::dvec2 DaylightXy(double kelvin)
        {
            const double t = kelvin * 1.4388 / 1.438;
            const double t2 = t * t;
            const double t3 = t2 * t;
            const double x = t <= 7000.0 ? -4.6070e9 / t3 + 2.9678e6 / t2 + 0.09911e3 / t + 0.244063
                                         : -2.0064e9 / t3 + 1.9018e6 / t2 + 0.24748e3 / t + 0.237040;
            return {x, -3.0 * x * x + 2.870 * x - 0.275};
        }

        /** @brief CIE xy to CIE 1960 uv, where distances off the locus are measured. */
        glm::dvec2 XyToUv(glm::dvec2 xy)
        {
            const double d = -2.0 * xy.x + 12.0 * xy.y + 3.0;
            return {4.0 * xy.x / d, 6.0 * xy.y / d};
        }

        /** @brief CIE 1960 uv back to CIE xy. */
        glm::dvec2 UvToXy(glm::dvec2 uv)
        {
            const double d = 2.0 * uv.x - 8.0 * uv.y + 4.0;
            return {3.0 * uv.x / d, 2.0 * uv.y / d};
        }

        /** @brief How far from the daylight locus a tint of 1 moves the white, in CIE 1960 uv (a Duv of 0.02). */
        constexpr double TintScale = 0.02;

        /**
         * @brief The white `temperature` and `tint` describe, in CIE XYZ at a luminance of 1: daylight at the
         * temperature (held to 2500 K to 25000 K), moved off the locus at right angles in CIE 1960 uv, towards green
         * for a positive tint.
         */
        glm::dvec3 WhiteXyz(float temperature, float tint)
        {
            const double kelvin = std::clamp(static_cast<double>(temperature), 2500.0, 25000.0);
            const glm::dvec2 uv = XyToUv(DaylightXy(kelvin));
            const glm::dvec2 along = glm::normalize(XyToUv(DaylightXy(kelvin * 1.001)) - uv);
            // Green lies at higher v than the locus.
            glm::dvec2 across(-along.y, along.x);
            if (across.y < 0.0)
            {
                across = -across;
            }
            const glm::dvec2 xy = UvToXy(uv + across * (static_cast<double>(tint) * TintScale));
            return {xy.x / xy.y, 1.0, (1.0 - xy.x - xy.y) / xy.y};
        }

        /**
         * @brief Linear Rec. 709 to itself, a von Kries adaptation in CAT02's LMS from the white `temperature` and
         * `tint` describe to D65, Rec. 709's white: so a surface lit by that white comes out grey. D65 is the daylight
         * formula's at 6500 K, within 0.0001 in xy of the published one, so 6500 K and no tint is the identity.
         */
        Math::Mat3 WhiteBalance(float temperature, float tint)
        {
            const glm::dmat3 toXyz = Rec709ToXyz();
            const glm::dmat3 cat02 = Cat02();
            const glm::dvec3 source = cat02 * WhiteXyz(temperature, tint);
            const glm::dvec3 destination = cat02 * WhiteXyz(6500.0f, 0.0f);
            const glm::dmat3 scale(glm::dvec3(destination.x / source.x, 0.0, 0.0),
                                   glm::dvec3(0.0, destination.y / source.y, 0.0),
                                   glm::dvec3(0.0, 0.0, destination.z / source.z));
            return Math::Mat3(glm::inverse(toXyz) * glm::inverse(cat02) * scale * cat02 * toXyz);
        }
    } // namespace

    bool IsIdentity(const ColourGradingSettings& settings) noexcept
    {
        return settings.temperature == 6500.0f && settings.tint == 0.0f && settings.contrast == 1.0f &&
               IsGrey(settings.shadowTint) && IsGrey(settings.highlightTint) && settings.saturation == 1.0f &&
               settings.slope == Vec3(1.0f) && settings.offset == Vec3(0.0f) && settings.power == Vec3(1.0f);
    }

    ColourGrade BakeColourGrade(const ColourGradingSettings& settings) noexcept
    {
        ColourGrade grade;
        grade.whiteBalance = WhiteBalance(settings.temperature, settings.tint);
        grade.contrast = std::max(settings.contrast, 0.0f);
        grade.shadowTint = Normalised(settings.shadowTint);
        grade.highlightTint = Normalised(settings.highlightTint);
        grade.split = -std::clamp(settings.balance, -1.0f, 1.0f) * BalanceStops;
        grade.saturation = std::max(settings.saturation, 0.0f);
        grade.slope = settings.slope;
        grade.offset = settings.offset;
        // pow(0, 0) is undefined in GLSL.
        grade.power = glm::max(settings.power, Vec3(0.001f));
        return grade;
    }

    Math::Vec3 ApplyColourGrade(const ColourGrade& grade, Math::Vec3 linear) noexcept
    {
        // 1. White balance.
        Vec3 colour = grade.whiteBalance * linear;
        // 2. Contrast, in log2 around mid grey: negatives (out of gamut after the white balance) clamped.
        colour = MidGrey * glm::pow(glm::max(colour / MidGrey, Vec3(1e-10f)), Vec3(grade.contrast));
        // 3. Split toning: from the shadow tint to the highlight tint across the split, by luminance in log2.
        const float stops = std::log2(std::max(Luminance(colour), 1e-10f) / MidGrey);
        const float highlight = glm::smoothstep(grade.split - SplitWidth, grade.split + SplitWidth, stops);
        colour *= glm::mix(grade.shadowTint, grade.highlightTint, highlight);
        // 4. Saturation, around luminance.
        colour = glm::mix(Vec3(Luminance(colour)), colour, grade.saturation);
        // 5. The ASC CDL: negatives clamped before the power.
        return glm::pow(glm::max(colour * grade.slope + grade.offset, Vec3(0.0f)), grade.power);
    }

    Math::Vec3 GradeColour(const ColourGradingSettings& settings, Math::Vec3 linear) noexcept
    {
        return IsIdentity(settings) ? linear : ApplyColourGrade(BakeColourGrade(settings), linear);
    }
} // namespace Fenrir::Render
