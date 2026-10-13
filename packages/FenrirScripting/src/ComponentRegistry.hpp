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
    // The table behind the engine's component lookups. Ids are positions, so they are only meaningful to the registry
    // that issued them: each registry has its own generation, and C# re-resolves a cached id when the generation
    // changes.
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

        // Forgets every component and event a script assembly defined (what a released generation leaves), keeping the
        // native ones. Ids are positions, so the ids of what remains may change; the generation changes with them, and
        // C# looks its ids up again.
        void RemoveScriptDefined()
        {
            std::vector<ComponentRegistration> keptComponents;
            for (auto& registration : m_components)
            {
                if (!registration.scriptDefined)
                {
                    keptComponents.push_back(std::move(registration));
                }
            }
            std::vector<EventRegistration> keptEvents;
            for (auto& registration : m_events)
            {
                if (!registration.scriptDefined)
                {
                    keptEvents.push_back(std::move(registration));
                }
            }
            m_components = std::move(keptComponents);
            m_events = std::move(keptEvents);
            m_ids.clear();
            m_eventIds.clear();
            for (std::size_t index = 0; index < m_components.size(); ++index)
            {
                m_ids.emplace(m_components[index].name, static_cast<std::int32_t>(index));
            }
            for (std::size_t index = 0; index < m_events.size(); ++index)
            {
                m_eventIds.emplace(m_events[index].name, static_cast<std::int32_t>(index));
            }
            m_generation = ++s_generations;
        }

        // Adds every registration, or none of them: false (and nothing added) if any has an empty or taken name, or the
        // same name twice.
        bool AddAll(std::vector<ComponentRegistration> registrations)
        {
            std::unordered_map<std::string, int> seen;
            for (const auto& registration : registrations)
            {
                if (registration.name.empty() || !registration.get || !registration.has ||
                    m_ids.contains(registration.name) || !seen.emplace(registration.name, 0).second)
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

        // Events: a second table with its own ids, under the same generation.

        // The id of an event by name for the C# type that claims it: the id, -1 if no event has the name, -3 if the
        // name belongs to an event of a different C# type.
        [[nodiscard]] std::int32_t ResolveEvent(std::string_view name, std::string_view typeName) const
        {
            const auto found = m_eventIds.find(std::string(name));
            if (found == m_eventIds.end())
            {
                return -1;
            }
            return m_events[static_cast<std::size_t>(found->second)].typeName == typeName ? found->second : -3;
        }

        // The id of the new event, or -1 if its name is empty or taken.
        std::int32_t AddEvent(EventRegistration registration)
        {
            if (registration.name.empty() || m_eventIds.contains(registration.name))
            {
                return -1;
            }
            const auto id = static_cast<std::int32_t>(m_events.size());
            m_eventIds.emplace(registration.name, id);
            m_events.push_back(std::move(registration));
            m_generation = ++s_generations;
            return id;
        }

        // Adds every event or none: false (and nothing added) if any has an empty or taken name, or the same name
        // twice.
        bool AddEvents(std::vector<EventRegistration> registrations)
        {
            std::unordered_map<std::string, int> seen;
            for (const auto& registration : registrations)
            {
                if (registration.name.empty() || m_eventIds.contains(registration.name) ||
                    !seen.emplace(registration.name, 0).second)
                {
                    return false;
                }
            }
            for (auto& registration : registrations)
            {
                (void)AddEvent(std::move(registration));
            }
            return true;
        }

        [[nodiscard]] std::int32_t FindEvent(std::string_view name) const
        {
            const auto found = m_eventIds.find(std::string(name));
            return found == m_eventIds.end() ? -1 : found->second;
        }

        [[nodiscard]] const EventRegistration* GetEvent(std::int32_t id) const noexcept
        {
            return id >= 0 && static_cast<std::size_t>(id) < m_events.size() ? &m_events[static_cast<std::size_t>(id)]
                                                                             : nullptr;
        }

        [[nodiscard]] const std::vector<EventRegistration>& Events() const noexcept
        {
            return m_events;
        }

        // The id registered under a name, or -1.
        [[nodiscard]] std::int32_t Find(std::string_view name) const
        {
            const auto found = m_ids.find(std::string(name));
            return found == m_ids.end() ? -1 : found->second;
        }

        [[nodiscard]] const ComponentRegistration* Get(std::int32_t id) const noexcept
        {
            return id >= 0 && static_cast<std::size_t>(id) < m_components.size()
                       ? &m_components[static_cast<std::size_t>(id)]
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
        std::vector<EventRegistration> m_events;
        std::unordered_map<std::string, std::int32_t> m_eventIds;
    };

    // Adds the components the engine itself provides to C#: Transform, Name and Relationship (read-only).
    [[nodiscard]] bool RegisterEngineComponents(ComponentRegistry& registry);
} // namespace Fenrir::Detail
