#pragma once

#include <cmath>
#include <cstdint>

namespace Fenrir
{
    /**
     * @brief A colour as linear red, green, blue and alpha, each a float, usually in [0, 1] (ADR-0025). Debug draw,
     * materials and lights take it, and the renderer packs it for vertices. Colours written as sRGB hex values, as
     * colour pickers and art tools give them, are converted when made (FromSrgb). Default constructed, it is opaque
     * white.
     */
    struct Colour
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;

        /**
         * @brief The colour written as `0xRRGGBBAA` in sRGB: red, green and blue are decoded through the sRGB curve to
         * linear, and alpha, which sRGB does not encode, is only scaled to [0, 1].
         */
        [[nodiscard]] static Colour FromSrgb(std::uint32_t rgba)
        {
            const auto byte = [rgba](int shift) { return static_cast<float>((rgba >> shift) & 0xFFu) / 255.0f; };
            return {SrgbToLinear(byte(24)), SrgbToLinear(byte(16)), SrgbToLinear(byte(8)), byte(0)};
        }

        /** @name Named colours
         * Opaque, in linear values. Grey is sRGB mid grey (`0x808080`), which looks halfway between black and white.
         * @{
         */
        static const Colour White;
        static const Colour Black;
        static const Colour Red;
        static const Colour Green;
        static const Colour Blue;
        static const Colour Yellow;
        static const Colour Cyan;
        static const Colour Magenta;
        static const Colour Grey;
        /** @} */

      private:
        // The sRGB transfer function's inverse (IEC 61966-2-1).
        static float SrgbToLinear(float encoded)
        {
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }
    };

    inline constexpr Colour Colour::White{1.0f, 1.0f, 1.0f, 1.0f};
    inline constexpr Colour Colour::Black{0.0f, 0.0f, 0.0f, 1.0f};
    inline constexpr Colour Colour::Red{1.0f, 0.0f, 0.0f, 1.0f};
    inline constexpr Colour Colour::Green{0.0f, 1.0f, 0.0f, 1.0f};
    inline constexpr Colour Colour::Blue{0.0f, 0.0f, 1.0f, 1.0f};
    inline constexpr Colour Colour::Yellow{1.0f, 1.0f, 0.0f, 1.0f};
    inline constexpr Colour Colour::Cyan{0.0f, 1.0f, 1.0f, 1.0f};
    inline constexpr Colour Colour::Magenta{1.0f, 0.0f, 1.0f, 1.0f};
    // FromSrgb(0x808080FF): ((128 / 255 + 0.055) / 1.055)^2.4.
    inline constexpr Colour Colour::Grey{0.2158605f, 0.2158605f, 0.2158605f, 1.0f};
} // namespace Fenrir
