#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    class ILogger;

    /** @brief A collision layer's name, and the mask a body in it collides with unless told otherwise. */
    struct CollisionLayer
    {
        std::string name;
        /** @brief The layers a body in this layer collides with by default, one bit per layer. */
        std::uint32_t mask = ~0u;
    };

    /**
     * @brief The collision layers named in PhysicsConfig, fixed at Physics::Install: lookups by name for game code,
     * logs and tools. Layers need no name to be used; plain bits work as well.
     */
    class CollisionLayers
    {
      public:
        /** @brief At most 32 layers, layer `i` named by `layers[i]`. Extra layers and repeated names are left out with
         * a warning to `logger`; an empty name leaves a layer unnamed. */
        CollisionLayers(std::span<const CollisionLayer> layers, ILogger& logger);

        /** @brief The bit of the layer called `name` (`1u << index`), or 0 if no layer has that name. */
        [[nodiscard]] std::uint32_t Bit(std::string_view name) const;

        /** @brief A filter for a body in the layer called `name`, colliding with that layer's default mask. A filter in
         * no layer (which collides with nothing) if no layer has that name. */
        [[nodiscard]] CollisionFilter Filter(std::string_view name) const;

        /** @brief The name of layer `index` (0 to 31), or an empty string if it has none. */
        [[nodiscard]] std::string_view Name(int index) const;

      private:
        std::vector<CollisionLayer> m_layers;
    };
} // namespace Fenrir
