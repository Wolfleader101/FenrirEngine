#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Backend/OpenGL/OpenGLDevice.hpp"

namespace Fenrir
{
    /**
     * @brief The GL tests' one way inside the OpenGL backend (the spec's seam 2): an offscreen target's pixels read
     * back, and whether GL still holds the object behind a handle. OpenGLDevice names it as a friend, so none of it is
     * part of the device's interface. Every function needs the OpenGL backend.
     */
    struct GlProbe
    {
        /** @brief One pixel as RGBA floats in [0, 1]. */
        using Pixel = std::array<float, 4>;

        /** @brief An offscreen target's pixels, with row 0 its top row however GL stores them. */
        struct Image
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::vector<float> rgba;

            /** @brief The pixel `x` from the left and `y` from the top. */
            [[nodiscard]] Pixel At(std::uint32_t x, std::uint32_t y) const
            {
                const std::size_t first = (std::size_t{y} * width + x) * 4;
                return {rgba.at(first), rgba.at(first + 1), rgba.at(first + 2), rgba.at(first + 3)};
            }
        };

        /** @brief The OpenGL device behind `device`. */
        [[nodiscard]] static Graphics::OpenGLDevice& Gl(Graphics::GraphicsDevice& device)
        {
            return dynamic_cast<Graphics::OpenGLDevice&>(device);
        }

        /** @brief Reads back mip 0 of an offscreen colour target; empty if it cannot be read. */
        [[nodiscard]] static Image Read(Graphics::GraphicsDevice& device, Graphics::TextureHandle texture)
        {
            Graphics::OpenGLDevice::Texels texels = Gl(device).ReadTexels(texture);
            return {texels.width, texels.height, std::move(texels.rgba)};
        }

        /** @brief The GL name behind a live buffer, else 0. */
        [[nodiscard]] static std::uint32_t GlName(Graphics::GraphicsDevice& device, Graphics::BufferHandle buffer)
        {
            return Gl(device).GlName(buffer);
        }

        /** @brief The GL name behind a live texture, else 0. */
        [[nodiscard]] static std::uint32_t GlName(Graphics::GraphicsDevice& device, Graphics::TextureHandle texture)
        {
            return Gl(device).GlName(texture);
        }

        /** @brief Whether GL still has a buffer object called `name`. */
        [[nodiscard]] static bool IsGlBuffer(Graphics::GraphicsDevice& device, std::uint32_t name)
        {
            return Gl(device).IsGlBuffer(name);
        }

        /** @brief Whether GL still has a texture object called `name`. */
        [[nodiscard]] static bool IsGlTexture(Graphics::GraphicsDevice& device, std::uint32_t name)
        {
            return Gl(device).IsGlTexture(name);
        }

        /** @brief Whether every channel of `actual` is within `tolerance` of `expected`'s. */
        [[nodiscard]] static bool Near(const Pixel& actual, const Pixel& expected, float tolerance = 2.0f / 255.0f)
        {
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                if (std::abs(actual.at(i) - expected.at(i)) > tolerance)
                {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace Fenrir
