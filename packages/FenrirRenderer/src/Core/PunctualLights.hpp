#pragma once

#include <cstdint>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // Point and spot lights (ADR-0048): what the frame's light buffer holds, and the CPU mirror of how
    // Fenrir/Lighting.glsl falls each one off, kept step for step with the shader so the CPU tests check its maths.
    // Rect lights share the buffer's record (Core/RectLights).

    /** @brief Which kind of light a light buffer record is; the shader reads it as a uint. */
    enum class LightKind : std::uint32_t
    {
        Point = 0,
        Spot = 1,
        /** @brief A rect light (Core/RectLights), shaded with linearly transformed cosines. */
        Rect = 2,
    };

    /** @brief One point or spot light read from the scene, in world space. */
    struct PunctualLight
    {
        LightKind kind = LightKind::Point;
        Math::Vec3 position{0.0f};
        /** @brief The way a spot light points (its entity's world -Z), unit length; unused by a point light. */
        Math::Vec3 direction{0.0f, 0.0f, -1.0f};
        /** @brief Its colour times its intensity, linear. */
        Math::Vec3 colour{0.0f};
        /** @brief The distance at which its light has fallen to zero, in world units. */
        float range = 0.0f;
        /** @brief A spot light's cone: full light inside `innerAngle`, none outside `outerAngle`, in radians. */
        float innerAngle = 0.0f;
        float outerAngle = 0.0f;
        /**
         * @brief Whether a point or spot light asks to cast shadows (ADR-0050), which its kind's shadow budget may
         * refuse.
         */
        bool castShadows = false;
        /** @brief A shadowed light's normal-offset bias, in its shadow's texels, as the sun's: at least 0. */
        float shadowNormalBias = 1.0f;
        /** @brief Its slope-scaled bias, in its shadow's texels per unit of slope, as the sun's: at least 0. */
        float shadowSlopeBias = 2.0f;
    };

    /**
     * @brief One record of the frame's light buffer, as `Fenrir/Frame.glsl` declares `FenrirLight` (std430): a point,
     * spot or rect light. Ticket 19's clusters index these records unchanged. A point light's cone scale is 0 and its
     * offset 1, so it takes the spot's cone falloff as 1 everywhere.
     */
    struct GpuLight
    {
        /** @brief Where a point or spot light is; a rect light's centre. */
        Math::Vec3 position{0.0f};
        float range = 0.0f;
        Math::Vec3 colour{0.0f};
        std::uint32_t kind = 0;
        /** @brief The way a spot light points, or a rect light faces (its entity's world -Z), unit length. */
        Math::Vec3 direction{0.0f, 0.0f, -1.0f};
        /** @brief 1 / (cos inner - cos outer): the cone falloff is smoothstep(cos x scale + offset). */
        float coneScale = 0.0f;
        /** @brief A rect light's right half: from its centre to the middle of its right edge (its world +X). */
        Math::Vec3 halfWidth{0.0f};
        /** @brief -cos outer x coneScale; 1 for a point light. */
        float coneOffset = 1.0f;
        /** @brief A rect light's top half: from its centre to the middle of its top edge (its world +Y). */
        Math::Vec3 halfHeight{0.0f};
        /** @brief 1 for a rect light that shines from both faces, else 0. */
        std::uint32_t twoSided = 0;
    };
    static_assert(sizeof(GpuLight) == 80);

    /** @brief `light` as its light buffer record. */
    [[nodiscard]] GpuLight ToGpuLight(const PunctualLight& light) noexcept;

    /**
     * @brief How a light of intensity 1 falls off at `distance`, windowed to zero at `range` (glTF's
     * `KHR_lights_punctual`, Karis 2013): 1 / d^2 times (1 - (d / range)^4)^2, clamped to [0, 1]. d^2 is held at
     * least 1e-4, so a pixel on the light takes 10000, not infinity. 0 at and past `range`.
     */
    [[nodiscard]] float DistanceFalloff(float distance, float range) noexcept;

    /**
     * @brief How a spot light falls off at `angle` radians off its axis: 1 inside `innerAngle`, 0 outside `outerAngle`,
     * and smoothstep between their cosines. An inner angle at or past the outer is held just inside it.
     */
    [[nodiscard]] float ConeFalloff(float angle, float innerAngle, float outerAngle) noexcept;

    /** @brief The cone falloff from a record's scale and offset at the cosine `cosAngle`, as the shader takes it. */
    [[nodiscard]] float ConeFalloff(float cosAngle, const GpuLight& light) noexcept;
} // namespace Fenrir::Render
