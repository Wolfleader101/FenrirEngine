#pragma once

#include "BridgeFunctions.hpp"
#include "ComponentState.hpp"
#include "Iris/Component.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace Iris::Detail
{
    /**
     * @brief Lets Iris's own tests build a component over a bridge function table they supply, so the native modules
     * can be checked against any response without a .NET runtime.
     *
     * Not part of the interface: this header is private to the library's sources, is not installed, and the tests that
     * use it are given its directory on their include path. Production code has no route to it.
     */
    struct TestAccess
    {
        /**
         * @brief A loaded component, as Host::LoadComponent would return it, whose calls go to `bridge` under `token`.
         * Destroying it releases the token through the table, as a real owner does.
         */
        [[nodiscard]] static Component MakeComponent(std::shared_ptr<const BridgeFunctions> bridge, std::uint64_t token)
        {
            auto state = std::make_unique<Component::State>();
            state->bridge = std::move(bridge);
            state->token = token;
            return Component(std::move(state));
        }
    };
} // namespace Iris::Detail
