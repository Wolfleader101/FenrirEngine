#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/Type.hpp"

namespace Iris
{
    // A type handle: the bridge table it was found through, the component's token and the type's id.
    // The bridge validates both ids on every call, so a handle needs no cleanup.
    struct Type::State
    {
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
        std::uint64_t component = 0;
        std::uint64_t type = 0;
    };
} // namespace Iris
