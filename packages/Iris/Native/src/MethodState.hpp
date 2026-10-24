#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/Method.hpp"

namespace Iris
{
    // A method handle: the bridge table it was found through, the component's token and the method's
    // id. The bridge validates both ids on every call, so a handle needs no cleanup.
    struct Detail::RawMethod::State
    {
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
        std::uint64_t component = 0;
        std::uint64_t method = 0;
        bool returnsBlock = false; // A successful string or struct result is then a block that must be given back.
    };
} // namespace Iris
