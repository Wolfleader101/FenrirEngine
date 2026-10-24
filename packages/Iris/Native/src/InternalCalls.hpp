#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/CallResult.hpp"

#include <memory>

namespace Iris::Detail
{
    // Remembers the live bridge and hands it every registration made so far (and, from now on, each new
    // one). Registering the same function again is harmless, so this can run on every start-up.
    CallResult<void> AttachInternalCalls(std::shared_ptr<const BridgeFunctions> bridge);
} // namespace Iris::Detail
