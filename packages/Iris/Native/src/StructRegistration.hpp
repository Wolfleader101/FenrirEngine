#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/CallResult.hpp"
#include "Iris/Method.hpp"

namespace Iris::Detail
{
    // The wire kind for a declared kind. A struct is registered (and so validated against the managed type,
    // after the structs nested in it) with the component first; its kind carries the bridge's handle.
    [[nodiscard]] CallResult<std::int32_t> ResolveWireKind(const BridgeFunctions& bridge, std::uint64_t component,
                                                           const KindSpec& spec);
} // namespace Iris::Detail
