#include "Core/PunctualLights.hpp"

#include <algorithm>
#include <cmath>

#include "Core/RectLights.hpp"

namespace Fenrir::Render
{
    namespace
    {
        /** @brief The least d^2 the falloff divides by, so a pixel on the light stays finite. */
        constexpr float MinDistanceSquared = 1e-4f;
        /** @brief The least gap between the cone's cosines, so its scale stays finite. */
        constexpr float MinConeGap = 1e-4f;
    } // namespace

    std::vector<GpuLight> LightRecords(std::span<const PunctualLight> lights, std::span<const RectLight> rectLights,
                                       std::span<const std::size_t> spotShadowed,
                                       std::span<const std::size_t> pointShadowed)
    {
        std::vector<GpuLight> records;
        records.reserve(lights.size() + rectLights.size());
        std::vector<bool> shadowedLight(lights.size(), false);
        for (const std::span<const std::size_t> chosen : {spotShadowed, pointShadowed})
        {
            for (const std::size_t i : chosen)
            {
                shadowedLight[i] = true;
                records.push_back(ToGpuLight(lights[i]));
            }
        }
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            if (!shadowedLight[i])
            {
                records.push_back(ToGpuLight(lights[i]));
            }
        }
        for (const RectLight& light : rectLights)
        {
            records.push_back(ToGpuLight(light));
        }
        return records;
    }

    GpuLight ToGpuLight(const PunctualLight& light) noexcept
    {
        GpuLight gpu;
        gpu.position = light.position;
        gpu.range = light.range;
        gpu.colour = light.colour;
        gpu.kind = static_cast<std::uint32_t>(light.kind);
        gpu.direction = light.direction;
        if (light.kind == LightKind::Spot)
        {
            const float cosOuter = std::cos(light.outerAngle);
            const float cosInner = std::max(std::cos(light.innerAngle), cosOuter + MinConeGap);
            gpu.coneScale = 1.0f / (cosInner - cosOuter);
            gpu.coneOffset = -cosOuter * gpu.coneScale;
        }
        return gpu;
    }

    float DistanceFalloff(float distance, float range) noexcept
    {
        const float d2 = distance * distance;
        if (range <= 0.0f || d2 >= range * range)
        {
            return 0.0f;
        }
        const float ratio2 = d2 / (range * range);
        const float window = std::clamp(1.0f - ratio2 * ratio2, 0.0f, 1.0f);
        return window * window / std::max(d2, MinDistanceSquared);
    }

    float ConeFalloff(float cosAngle, const GpuLight& light) noexcept
    {
        const float t = std::clamp(cosAngle * light.coneScale + light.coneOffset, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float ConeFalloff(float angle, float innerAngle, float outerAngle) noexcept
    {
        PunctualLight spot;
        spot.kind = LightKind::Spot;
        spot.innerAngle = innerAngle;
        spot.outerAngle = outerAngle;
        return ConeFalloff(std::cos(angle), ToGpuLight(spot));
    }
} // namespace Fenrir::Render
