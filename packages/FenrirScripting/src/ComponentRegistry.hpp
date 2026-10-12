#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir::Detail
{
    // The table behind the engine's component lookups. Ids are positions, so they are only meaningful to the registry that
    // issued them: each registry has its own generation, and C# re-resolves a cached id when the generation changes.
    class ComponentRegistry
    {
      public:
        ComponentRegistry() : m_generation(++s_generations)
        {
        }

        // The id of a component by name for the C# type that claims it: the id, -1 if nothing is registered under the
        // name, or -3 if the name is registered for a different C# type.
        [[nodiscard]] std::int32_t Resolve(std::string_view name, std::string_view typeName) const
        {
            const std::int32_t id = Find(name);
            if (id < 0)
            {
                return id;
            }
            return Get(id)->typeName == typeName ? id : -3;
        }

        // The id of the new registration, or -1 if the name is empty or already taken.
        std::int32_t Add(ComponentRegistration registration)
        {
            if (registration.name.empty() || !registration.get || !registration.has ||
                m_ids.contains(registration.name))
            {
                return -1;
            }
            const auto id = static_cast<std::int32_t>(m_components.size());
            m_ids.emplace(registration.name, id);
            m_components.push_back(std::move(registration));
            // The table changed, so any id or "not registered" that C# remembered is stale.
            m_generation = ++s_generations;
            return id;
        }

        // Adds every registration, or none of them: false (and nothing added) if any has an empty or taken name, or the same name
        // twice.
        bool AddAll(std::vector<ComponentRegistration> registrations)
        {
            std::unordered_map<std::string, int> seen;
            for (const auto& registration : registrations)
            {
                if (registration.name.empty() || !registration.get || !registration.has || m_ids.contains(registration.name) ||
                    !seen.emplace(registration.name, 0).second)
                {
                    return false;
                }
            }
            for (auto& registration : registrations)
            {
                (void)Add(std::move(registration));
            }
            return true;
        }

        // The id registered under a name, or -1.
        [[nodiscard]] std::int32_t Find(std::string_view name) const
        {
            const auto found = m_ids.find(std::string(name));
            return found == m_ids.end() ? -1 : found->second;
        }

        [[nodiscard]] const ComponentRegistration* Get(std::int32_t id) const noexcept
        {
            return id >= 0 && static_cast<std::size_t>(id) < m_components.size() ? &m_components[static_cast<std::size_t>(id)]
                                                                                  : nullptr;
        }

        [[nodiscard]] const std::vector<ComponentRegistration>& All() const noexcept
        {
            return m_components;
        }

        [[nodiscard]] std::uint32_t Generation() const noexcept
        {
            return m_generation;
        }

      private:
        inline static std::atomic<std::uint32_t> s_generations{0};

        std::uint32_t m_generation;
        std::vector<ComponentRegistration> m_components;
        std::unordered_map<std::string, std::int32_t> m_ids;
    };

    // Adds the components the engine itself provides to C#: Transform, Name and Relationship (read-only).
    [[nodiscard]] bool RegisterEngineComponents(ComponentRegistry& registry);
} // namespace Fenrir::Detail
