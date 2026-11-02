#include "Core/RectLights.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/packing.hpp>

#include "Core/LtcTables.hpp"

namespace Fenrir::Render
{
    namespace
    {
        static_assert(Generated::LtcMatrixTexels.size() == std::size_t{LtcSide} * LtcSide * 4);
        static_assert(Generated::LtcAmplitudeTexels.size() == std::size_t{LtcSide} * LtcSide * 4);

        /** @brief The texel at column `x` and row `y` of `texels`, clamped to the table. */
        Math::Vec4 TexelAt(std::span<const std::uint16_t> texels, int x, int y) noexcept
        {
            constexpr int Last = static_cast<int>(LtcSide) - 1;
            const std::size_t index = (static_cast<std::size_t>(std::clamp(y, 0, Last)) * LtcSide +
                                       static_cast<std::size_t>(std::clamp(x, 0, Last))) *
                                      4;
            return {glm::unpackHalf1x16(texels[index]), glm::unpackHalf1x16(texels[index + 1]),
                    glm::unpackHalf1x16(texels[index + 2]), glm::unpackHalf1x16(texels[index + 3])};
        }
    } // namespace

    GpuLight ToGpuLight(const RectLight& light) noexcept
    {
        GpuLight gpu;
        gpu.position = light.position;
        gpu.range = light.range;
        gpu.colour = light.colour;
        gpu.kind = static_cast<std::uint32_t>(LightKind::Rect);
        gpu.direction = glm::normalize(glm::cross(light.up, light.right));
        gpu.halfWidth = light.right * (light.width * 0.5f);
        gpu.halfHeight = light.up * (light.height * 0.5f);
        gpu.twoSided = light.twoSided ? 1u : 0u;
        return gpu;
    }

    std::span<const std::uint16_t> LtcMatrixTexels() noexcept
    {
        return Generated::LtcMatrixTexels;
    }

    std::span<const std::uint16_t> LtcAmplitudeTexels() noexcept
    {
        return Generated::LtcAmplitudeTexels;
    }

    Math::Vec4 SampleLtcTable(std::span<const std::uint16_t> texels, Math::Vec2 uv) noexcept
    {
        // Texel centres sit at (i + 0.5) / side, as GL's linear filtering takes them.
        const Math::Vec2 at = uv * static_cast<float>(LtcSide) - 0.5f;
        const Math::Vec2 base = glm::floor(at);
        const Math::Vec2 t = at - base;
        const int x = static_cast<int>(base.x);
        const int y = static_cast<int>(base.y);
        const Math::Vec4 top = glm::mix(TexelAt(texels, x, y), TexelAt(texels, x + 1, y), t.x);
        const Math::Vec4 bottom = glm::mix(TexelAt(texels, x, y + 1), TexelAt(texels, x + 1, y + 1), t.x);
        return glm::mix(top, bottom, t.y);
    }

    Math::Vec2 LtcUv(Math::Vec2 lookup) noexcept
    {
        constexpr float Side = static_cast<float>(LtcSide);
        return lookup * ((Side - 1.0f) / Side) + 0.5f / Side;
    }

    Math::Vec3 LtcEdge(Math::Vec3 from, Math::Vec3 to) noexcept
    {
        const float x = glm::dot(from, to);
        const float y = std::abs(x);
        const float a = 0.8543985f + (0.4965155f + 0.0145206f * y) * y;
        const float b = 3.4175940f + (4.1616724f + y) * y;
        const float v = a / b;
        const float thetaSinTheta = x > 0.0f ? v : 0.5f / std::sqrt(std::max(1.0f - x * x, 1e-7f)) - v;
        return glm::cross(from, to) * thetaSinTheta;
    }

    float LtcIntegral(Math::Vec3 point, const Math::Mat3& toLobe, const GpuLight& light) noexcept
    {
        const Math::Vec3 centre = light.position - point;
        // Behind the light: on the side it does not face.
        const bool behind = glm::dot(centre, light.direction) > 0.0f;
        if (behind && light.twoSided == 0)
        {
            return 0.0f;
        }
        const std::array<Math::Vec3, 4> corners{
            glm::normalize(toLobe * (centre - light.halfWidth - light.halfHeight)),
            glm::normalize(toLobe * (centre + light.halfWidth - light.halfHeight)),
            glm::normalize(toLobe * (centre + light.halfWidth + light.halfHeight)),
            glm::normalize(toLobe * (centre - light.halfWidth + light.halfHeight)),
        };
        const Math::Vec3 sum = LtcEdge(corners[0], corners[1]) + LtcEdge(corners[1], corners[2]) +
                               LtcEdge(corners[2], corners[3]) + LtcEdge(corners[3], corners[0]);
        const float formFactor = glm::length(sum);
        // Seen from behind, the corners wind the other way.
        const float elevation = (behind ? -sum.z : sum.z) / std::max(formFactor, 1e-7f);
        const float horizon = SampleLtcTable(LtcAmplitudeTexels(), LtcUv({elevation * 0.5f + 0.5f, formFactor})).w;
        return formFactor * horizon;
    }

    float RectWindow(Math::Vec3 point, const GpuLight& light) noexcept
    {
        const Math::Vec3 offset = point - light.position;
        const float across =
            std::clamp(glm::dot(offset, light.halfWidth) / glm::dot(light.halfWidth, light.halfWidth), -1.0f, 1.0f);
        const float along =
            std::clamp(glm::dot(offset, light.halfHeight) / glm::dot(light.halfHeight, light.halfHeight), -1.0f, 1.0f);
        const Math::Vec3 gap = offset - light.halfWidth * across - light.halfHeight * along;
        const float distanceSquared = glm::dot(gap, gap);
        const float rangeSquared = light.range * light.range;
        if (distanceSquared >= rangeSquared)
        {
            return 0.0f;
        }
        const float ratio = distanceSquared / rangeSquared;
        const float window = std::clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
        return window * window;
    }

    Math::Mat3 LtcSurfaceFrame(Math::Vec3 normal, Math::Vec3 view) noexcept
    {
        Math::Vec3 tangent = view - normal * glm::dot(normal, view);
        if (glm::dot(tangent, tangent) < 1e-8f)
        {
            // Seen straight on, the lobe is round about the normal: any tangent will do.
            tangent = glm::cross(normal, std::abs(normal.x) < 0.9f ? Math::Vec3(1.0f, 0.0f, 0.0f)
                                                                   : Math::Vec3(0.0f, 1.0f, 0.0f));
        }
        tangent = glm::normalize(tangent);
        const Math::Vec3 bitangent = glm::cross(normal, tangent);
        return glm::transpose(Math::Mat3(tangent, bitangent, normal));
    }

    float RectDiffuse(Math::Vec3 point, Math::Vec3 normal, const GpuLight& light) noexcept
    {
        return LtcIntegral(point, LtcSurfaceFrame(normal, normal), light);
    }
} // namespace Fenrir::Render
