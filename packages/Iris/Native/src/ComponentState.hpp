#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/Component.hpp"

namespace Iris
{
    struct Component::State
    {
        // The bridge table this owner was loaded with; it outlives host reset and destruction.
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
        // Nonzero while loaded.
        std::uint64_t token = 0;
        // The released token, kept only to observe and later forget unloading.
        std::uint64_t trackingToken = 0;
        ~State() noexcept;
    };
} // namespace Iris
