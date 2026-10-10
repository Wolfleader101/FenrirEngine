#pragma once

#include "BridgeFunctions.hpp"
#include "FenrirDotNetHost/ManagedComponent.hpp"

namespace Fenrir::Scripting
{
    struct ManagedComponent::State
    {
        // Immutable resident entry points, independent of host readiness/reset.
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
        std::uint64_t token = 0;
        std::uint64_t trackingToken = 0;
        bool isCollectible = false;
        ~State() noexcept;
    };
} // namespace Fenrir::Scripting
