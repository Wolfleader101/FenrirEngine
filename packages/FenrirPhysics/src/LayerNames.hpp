#pragma once

// How logs name collision layers. Private to the FenrirPhysics package.

#include <cstdint>
#include <string>

namespace Fenrir
{
    class CollisionLayers;

    /**
     * @brief The layers in `bits`, for a log message: each layer's name from `names` (CollisionLayers::Name), or
     * "layer i" for one with no name, joined by ", "; "no layer" for none. Layers are logged by number alone when
     * `names` is null.
     */
    [[nodiscard]] std::string LayerNames(const CollisionLayers* names, std::uint32_t bits);
} // namespace Fenrir
