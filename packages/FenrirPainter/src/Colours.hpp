#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::PainterDetail
{
    /** @brief `colour`'s channels sRGB-encoded and premultiplied by its alpha, each clamped to [0, 1]. */
    inline Math::Vec4 SrgbPremultiplied(const Colour& colour)
    {
        const auto encode = [](float linear) {
            const float c = std::clamp(linear, 0.0f, 1.0f);
            if (c >= 1.0f)
            {
                // The curve's float result at 1 falls just short of 1, which would round half alpha down.
                return 1.0f;
            }
            return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        };
        const float a = std::clamp(colour.a, 0.0f, 1.0f);
        return {encode(colour.r) * a, encode(colour.g) * a, encode(colour.b) * a, a};
    }

    /** @brief `value`'s channels as RGBA8, red in the lowest byte. */
    inline std::uint32_t Pack(const Math::Vec4& value)
    {
        const auto byte = [](float channel) {
            return static_cast<std::uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
        };
        return byte(value.x) | (byte(value.y) << 8u) | (byte(value.z) << 16u) | (byte(value.w) << 24u);
    }

    /** @brief `colour` as the painter's vertices hold it: sRGB-encoded, premultiplied, RGBA8. */
    inline std::uint32_t PackColour(const Colour& colour)
    {
        return Pack(SrgbPremultiplied(colour));
    }
} // namespace Fenrir::PainterDetail
