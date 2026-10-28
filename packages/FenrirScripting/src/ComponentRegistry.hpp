#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/Errors.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir::Detail
{
    // A number no registry has had before: each registry's generation starts as one, and every change to a registry
    // takes the next.
    [[nodiscard]] inline std::uint32_t NextRegistryGeneration() noexcept
    {
        static std::atomic<std::uint32_t> generations{0};
        return ++generations;
    }

    // One kind of thing the engine exposes to C# by name: the table behind the registry's components, events, shared
    // structs and resources alike. Ids are positions, so they are only meaningful to the registry that issued them;
    // every change to a table moves its registry's generation on, and C# re-resolves a cached id when the generation
    // changes. A name names one thing across every table of the registry: `names` is the registry's set of all of
    // them, which each table consults and keeps.
    template <typename Registration>
    class RegistryTable
    {
      public:
        RegistryTable(std::uint32_t& generation, std::unordered_set<std::string>& names, std::string_view noun)
            : m_generation(generation), m_names(names), m_noun(noun)
        {
        }

        // What this table holds, as its callers' messages say it: "component", "event", "shared struct", "resource".
        [[nodiscard]] std::string_view Noun() const noexcept
        {
            return m_noun;
        }

        // The id registered under `name` for the C# type that claims it, or a RegistryError as its value: NotRegistered
        // if nothing is registered under the name, WrongType if it is registered for a different C# type.
        [[nodiscard]] std::int32_t Resolve(std::string_view name, std::string_view typeName) const
        {
            const std::int32_t id = Find(name);
            if (id < 0)
            {
                return id;
            }
            return Get(id)->typeName == typeName ? id : static_cast<std::int32_t>(RegistryError::WrongType);
        }

        // The id of the new registration, or RegistryError::NotRegistered as its value if the name is empty or any
        // table of the registry already holds it.
        std::int32_t Add(Registration registration)
        {
            if (!Usable(registration) || m_names.contains(registration.name))
            {
                return static_cast<std::int32_t>(RegistryError::NotRegistered);
            }
            const auto id = static_cast<std::int32_t>(m_registrations.size());
            m_names.insert(registration.name);
            m_ids.emplace(registration.name, id);
            m_registrations.push_back(std::move(registration));
            // The table changed, so any id or "not registered" that C# remembered is stale.
            Bump();
            return id;
        }

        // Adds every registration, or none of them: false (and nothing added) if any has an empty or taken name, or
        // the same name twice.
        bool AddAll(std::vector<Registration> registrations)
        {
            std::unordered_set<std::string> seen;
            for (const auto& registration : registrations)
            {
                if (!Usable(registration) || m_names.contains(registration.name) ||
                    !seen.insert(registration.name).second)
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

        // Forgets every registration `remove` selects. Ids are positions, so the ids of what remains may change; the
        // generation changes with them, and C# looks its ids up again.
        template <typename Remove>
        void RemoveWhere(Remove remove)
        {
            std::vector<Registration> kept;
            for (auto& registration : m_registrations)
            {
                if (remove(registration))
                {
                    m_names.erase(registration.name);
                }
                else
                {
                    kept.push_back(std::move(registration));
                }
            }
            m_registrations = std::move(kept);
            m_ids.clear();
            for (std::size_t index = 0; index < m_registrations.size(); ++index)
            {
                m_ids.emplace(m_registrations[index].name, static_cast<std::int32_t>(index));
            }
            Bump();
        }

        // The id registered under a name, or RegistryError::NotRegistered as its value.
        [[nodiscard]] std::int32_t Find(std::string_view name) const
        {
            const auto found = m_ids.find(std::string(name));
            return found == m_ids.end() ? static_cast<std::int32_t>(RegistryError::NotRegistered) : found->second;
        }

        [[nodiscard]] const Registration* Get(std::int32_t id) const noexcept
        {
            return id >= 0 && static_cast<std::size_t>(id) < m_registrations.size()
                       ? &m_registrations[static_cast<std::size_t>(id)]
                       : nullptr;
        }

        [[nodiscard]] const std::vector<Registration>& All() const noexcept
        {
            return m_registrations;
        }

      private:
        [[nodiscard]] static bool Usable(const Registration& registration)
        {
            return !registration.name.empty();
        }

        void Bump()
        {
            m_generation = NextRegistryGeneration();
        }

        std::uint32_t& m_generation;
        std::unordered_set<std::string>& m_names;
        std::string_view m_noun;
        std::vector<Registration> m_registrations;
        std::unordered_map<std::string, std::int32_t> m_ids;
    };

    // The tables behind the engine's lookups by name: its components, events, shared structs and resources, each a
    // RegistryTable with its own ids, under one generation and one rule that a name names one of them. Shared structs
    // have ids like the rest, which nothing asks for: they are kept so each load checks them against their C# structs.
    class ComponentRegistry
    {
      public:
        ComponentRegistry() = default;
        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        [[nodiscard]] RegistryTable<ComponentRegistration>& Components() noexcept
        {
            return m_components;
        }

        [[nodiscard]] const RegistryTable<ComponentRegistration>& Components() const noexcept
        {
            return m_components;
        }

        [[nodiscard]] RegistryTable<EventRegistration>& Events() noexcept
        {
            return m_events;
        }

        [[nodiscard]] const RegistryTable<EventRegistration>& Events() const noexcept
        {
            return m_events;
        }

        [[nodiscard]] RegistryTable<SharedStructRegistration>& SharedStructs() noexcept
        {
            return m_sharedStructs;
        }

        [[nodiscard]] const RegistryTable<SharedStructRegistration>& SharedStructs() const noexcept
        {
            return m_sharedStructs;
        }

        [[nodiscard]] RegistryTable<ResourceRegistration>& Resources() noexcept
        {
            return m_resources;
        }

        [[nodiscard]] const RegistryTable<ResourceRegistration>& Resources() const noexcept
        {
            return m_resources;
        }

        // Registers everything `extension` gives C# by name, moving its lists into the tables in the order Install
        // always used: components, events (each event's queue is named in `app` before the event is added, since the
        // queue is the App's), shared structs, resources, all under the one rule that a name names one thing across
        // every kind. Returns why it stopped, phrased for Scripting::Install's startup failure, or nothing. A failure
        // midway leaves what was added before it: Install discards the registry with the Scripting it was building, so
        // nothing reads a half-registered extension.
        [[nodiscard]] std::optional<std::string> AddExtension(ScriptingExtension& extension, App& app)
        {
            const std::string source = "extension '" + extension.name + "'";
            const auto nothing = [](const auto&) -> std::optional<std::string> { return std::nullopt; };
            const auto nameQueue = [&](const EventRegistration& registration) -> std::optional<std::string> {
                if (registration.bind && !registration.bind(app))
                {
                    return "the event '" + registration.name + "' in " + source +
                           " names a queue the app already has under another type";
                }
                return std::nullopt;
            };
            if (auto failure = AddAllOf(extension.components, m_components, source, nothing))
            {
                return failure;
            }
            if (auto failure = AddAllOf(extension.events, m_events, source, nameQueue))
            {
                return failure;
            }
            if (auto failure = AddAllOf(extension.sharedStructs, m_sharedStructs, source, nothing))
            {
                return failure;
            }
            return AddAllOf(extension.resources, m_resources, source, nothing);
        }

        // Forgets every component and event a script assembly defined (what a released generation leaves), keeping the
        // native ones. Only those two tables take script-defined entries.
        void RemoveScriptDefined()
        {
            m_components.RemoveWhere(
                [](const ComponentRegistration& registration) { return registration.scriptDefined; });
            m_events.RemoveWhere([](const EventRegistration& registration) { return registration.scriptDefined; });
        }

        [[nodiscard]] std::uint32_t Generation() const noexcept
        {
            return m_generation;
        }

      private:
        // Moves `registrations` into `table`, running `before` on each first; the message names the kind as the table's
        // noun ("a component", "an event") and the extension it came from.
        template <typename Registration, typename Before>
        static std::optional<std::string> AddAllOf(std::vector<Registration>& registrations,
                                                   RegistryTable<Registration>& table, const std::string& source,
                                                   Before before)
        {
            for (auto& registration : registrations)
            {
                if (auto failure = before(registration))
                {
                    return failure;
                }
                if (table.Add(std::move(registration)) < 0)
                {
                    const std::string noun(table.Noun());
                    const char* article =
                        std::string_view("aeiou").find(noun[0]) == std::string_view::npos ? "a " : "an ";
                    return article + noun + " in " + source + " has an empty or duplicate name";
                }
            }
            return std::nullopt;
        }

        std::uint32_t m_generation = NextRegistryGeneration();
        std::unordered_set<std::string> m_names;
        RegistryTable<ComponentRegistration> m_components{m_generation, m_names, "component"};
        RegistryTable<EventRegistration> m_events{m_generation, m_names, "event"};
        RegistryTable<SharedStructRegistration> m_sharedStructs{m_generation, m_names, "shared struct"};
        RegistryTable<ResourceRegistration> m_resources{m_generation, m_names, "resource"};
    };
} // namespace Fenrir::Detail
