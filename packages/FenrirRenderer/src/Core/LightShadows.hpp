#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/PunctualLights.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // What spot and point shadows share (ADR-0050): each kind casts within a budget, the lights nearest to the views
    // keeping their shadows each frame, drawn into maps pooled by the budget.

    /** @brief How one kind of light's shadow maps are made, from RendererConfig: its spot or point shadow members. */
    struct ShadowBudgetConfig
    {
        /** @brief How many lights of the kind may cast shadows in a frame; 0 shadows none. */
        std::uint32_t budget = 0;
        /** @brief The side of each light's map, in texels: a spot light's atlas tile, or a point light's cube face. */
        std::uint32_t mapSize = 0;
    };

    /** @brief Which spot or point lights cast this frame: their places in the frame's light list, nearest first. */
    struct ShadowChoice
    {
        /** @brief At most the budget of them. */
        std::vector<std::size_t> lights;
        /** @brief How many lights of the kind asked to cast: more than `lights` when the budget left some out. */
        std::size_t wanted = 0;
    };

    /**
     * @brief The lights of `kind` in `lights` with castShadows that keep their shadows: the `budget` nearest to any of
     * the cameras at `cameras` (the distance from each light's position to the nearest camera), the nearer first, ties
     * in list order. With no camera, the first `budget` in list order.
     */
    [[nodiscard]] ShadowChoice ChooseShadowedLights(std::span<const PunctualLight> lights,
                                                    std::span<const Math::Vec3> cameras, std::uint32_t budget,
                                                    LightKind kind);
} // namespace Fenrir::Render
