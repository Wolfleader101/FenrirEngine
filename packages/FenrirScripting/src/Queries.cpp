#include "Queries.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace Fenrir::Detail
{
    namespace
    {
        // The component types for the ids, in order, or empty if any is unknown.
        std::vector<ComponentType> Resolve(const ComponentRegistry& registry, std::span<const std::int32_t> ids)
        {
            std::vector<ComponentType> types;
            types.reserve(ids.size());
            for (const std::int32_t id : ids)
            {
                const auto* registration = registry.Get(id);
                if (registration == nullptr)
                {
                    return {};
                }
                types.push_back(registration->type);
            }
            return types;
        }
    } // namespace

    std::int32_t CountQueryRows(EntityList& entities, const ComponentRegistry& registry,
                                std::span<const std::int32_t> ids)
    {
        const auto types = Resolve(registry, ids);
        if (types.empty())
        {
            return ids.empty() ? 0 : -1;
        }
        // The smallest pool is a cheap way out when it is empty; otherwise the ECS counts the matches exactly, so a
        // rare match among millions of entities does not cost millions of rows.
        for (const ComponentType& type : types)
        {
            if (entities.Count(type) == 0)
            {
                return 0;
            }
        }
        std::size_t matches = 0;
        entities.ForEachWith(types, [&matches](std::uint32_t) { ++matches; });
        return static_cast<std::int32_t>(std::min<std::size_t>(matches, std::numeric_limits<std::int32_t>::max()));
    }

    std::int32_t FillQueryRows(EntityList& entities, const ComponentRegistry& registry,
                               std::span<const std::int32_t> ids, std::uintptr_t* rows, std::int32_t capacity)
    {
        const auto types = Resolve(registry, ids);
        if (types.empty())
        {
            return ids.empty() ? 0 : -1;
        }

        const std::size_t stride = types.size() + 1;
        std::int32_t written = 0;
        entities.ForEachWith(types, [&](std::uint32_t entity) {
            if (written >= capacity)
            {
                return;
            }
            std::uintptr_t* row = rows + static_cast<std::size_t>(written) * stride;
            row[0] = entity;
            for (std::size_t index = 0; index < types.size(); ++index)
            {
                void* component = entities.Get(types[index], entity);
                if (component == nullptr)
                {
                    return; // Cannot happen for an entity the query selected; never hand out a null pointer.
                }
                row[index + 1] = reinterpret_cast<std::uintptr_t>(component);
            }
            ++written;
        });
        return written;
    }
} // namespace Fenrir::Detail
