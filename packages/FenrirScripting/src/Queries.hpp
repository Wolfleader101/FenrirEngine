#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ComponentRegistry.hpp"

namespace Fenrir::Detail
{
    // How many entities have every listed component, or -1 if any id is not a registered component.
    [[nodiscard]] std::int32_t CountQueryRows(EntityList& entities, const ComponentRegistry& registry,
                                              std::span<const std::int32_t> ids);

    // Writes a row for each entity that has every listed component, up to `capacity` rows, and returns how many it
    // wrote. A row is `ids.size() + 1` words: the entity value, then the address of its component for each id in order.
    // Negative if an id is not a registered component.
    [[nodiscard]] std::int32_t FillQueryRows(EntityList& entities, const ComponentRegistry& registry,
                                             std::span<const std::int32_t> ids, std::uintptr_t* rows,
                                             std::int32_t capacity);
} // namespace Fenrir::Detail
