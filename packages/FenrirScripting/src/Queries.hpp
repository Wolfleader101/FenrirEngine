#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ComponentRegistry.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir::Detail
{
    // The words a query row spends on the entity: a Fenrir::Entity, as the engine lays it out, which C# reads whole
    // through its mirror (so C#'s QueryNative.EntityWords is the same size over the same word).
    inline constexpr std::size_t EntityWords = sizeof(Entity) / sizeof(std::uintptr_t);
    static_assert(sizeof(Entity) == EntityWords * sizeof(std::uintptr_t) && alignof(Entity) <= alignof(std::uintptr_t),
                  "a query row holds the entity as the engine lays it out: value, then list");

    // How many entities have every listed component, or -1 if any id is not a registered component.
    [[nodiscard]] std::int32_t CountQueryRows(EntityList& entities, const ComponentRegistry& registry,
                                              std::span<const std::int32_t> ids);

    // Writes a row for each entity that has every listed component, up to `capacity` rows, and returns how many it
    // wrote. A row is `ids.size() + EntityWords` words: the entity as the engine holds it (a Fenrir::Entity, its value
    // and its list, so C# reads an entity with its scene set), then the address of its component for each id in order.
    // Negative if an id is not a registered component.
    [[nodiscard]] std::int32_t FillQueryRows(EntityList& entities, const ComponentRegistry& registry,
                                             std::span<const std::int32_t> ids, std::uintptr_t* rows,
                                             std::int32_t capacity);
} // namespace Fenrir::Detail
