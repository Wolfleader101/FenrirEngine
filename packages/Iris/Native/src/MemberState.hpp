#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/Member.hpp"

namespace Iris
{
    // A member handle: the bridge table it was found through, the component's token and the member's id.
    // The bridge validates both ids on every call, so a handle needs no cleanup.
    struct Detail::RawMember::State
    {
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
        std::uint64_t component = 0;
        std::uint64_t member = 0;
        bool returnsString = false; // A successful get is then a block that must be given back.
    };
} // namespace Iris
