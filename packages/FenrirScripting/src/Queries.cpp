#include "Queries.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace Fenrir::Detail
{
    namespace
    {
        // The registrations for the ids, or empty if any is unknown or cannot be queried.
        std::vector<const ComponentRegistration*> Resolve(const ComponentRegistry& registry,
                                                          std::span<const std::int32_t> ids)
        {
            std::vector<const ComponentRegistration*> found;
            found.reserve(ids.size());
            for (const std::int32_t id : ids)
            {
                const auto* registration = registry.Get(id);
                if (registration == nullptr || !registration->selector || !registration->count)
                {
                    return {};
                }
                found.push_back(registration);
            }
            return found;
        }
    } // namespace

    std::int32_t CountQueryRows(EntityList& entities, const ComponentRegistry& registry,
                                std::span<const std::int32_t> ids)
    {
        const auto found = Resolve(registry, ids);
        if (found.empty())
        {
            return ids.empty() ? 0 : -1;
        }
        // The smallest pool is a cheap way out when it is empty; otherwise the ECS counts the matches exactly, so a
        // rare match among millions of entities does not cost millions of rows.
        for (const auto* registration : found)
        {
            if (registration->count(entities) == 0)
            {
                return 0;
            }
        }
        std::vector<ComponentSelector> selectors;
        selectors.reserve(found.size());
        for (const auto* registration : found)
        {
            selectors.push_back(*registration->selector);
        }
        std::size_t matches = 0;
        entities.ForEachWith(selectors, [&matches](std::uint32_t) { ++matches; });
        return static_cast<std::int32_t>(std::min<std::size_t>(matches, std::numeric_limits<std::int32_t>::max()));
    }

    std::int32_t FillQueryRows(EntityList& entities, const ComponentRegistry& registry,
                               std::span<const std::int32_t> ids, std::uintptr_t* rows, std::int32_t capacity)
    {
        const auto found = Resolve(registry, ids);
        if (found.empty())
        {
            return ids.empty() ? 0 : -1;
        }
        std::vector<ComponentSelector> selectors;
        selectors.reserve(found.size());
        for (const auto* registration : found)
        {
            selectors.push_back(*registration->selector);
        }

        const std::size_t stride = found.size() + 1;
        std::int32_t written = 0;
        entities.ForEachWith(selectors, [&](std::uint32_t entity) {
            if (written >= capacity)
            {
                return;
            }
            std::uintptr_t* row = rows + static_cast<std::size_t>(written) * stride;
            row[0] = entity;
            for (std::size_t index = 0; index < found.size(); ++index)
            {
                void* component = found[index]->get(entities, entity);
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
