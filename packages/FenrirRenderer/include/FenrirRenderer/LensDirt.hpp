#pragma once

#include <cstdint>

#include "FenrirAssets/ImageData.hpp"

namespace Fenrir
{
    /**
     * @brief A lens dirt image for LensFlare::dirt, made from `seed`: a faint haze, soft smudges, fine specks of dust
     * and a few hair-like streaks, light on black, as greyscale RGBA8 in linear light. Black is a clean lens. The same
     * size and seed always make the same image. `width` and `height` are at least 1.
     */
    [[nodiscard]] ImageData GenerateLensDirt(std::uint32_t width, std::uint32_t height, std::uint32_t seed = 1);
} // namespace Fenrir
