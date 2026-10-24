#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/Object.hpp"

namespace Iris
{
    // An owned object: the bridge table it was created through, its component's token and its id. The
    // destructor releases it, so it must never be copied.
    struct Object::State
    {
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
        std::uint64_t component = 0;
        std::uint64_t object = 0;
        ~State() noexcept;
    };
} // namespace Iris
