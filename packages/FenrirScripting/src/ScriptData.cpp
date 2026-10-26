#include "ScriptData.hpp"

#include "ScriptComponents.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Fenrir::Detail
{
    namespace
    {
        using Description = ScriptComponentDescription;
        using Result = Iris::CallResult<void>;

        Result Succeeded()
        {
            return Result{Iris::CallStatus::Success, {}, false};
        }

        Result Failed(std::string diagnostic)
        {
            return Result{Iris::CallStatus::InvalidArgument, std::move(diagnostic), false};
        }

        Result ValidateAll(const std::vector<ComponentRegistration>& registrations, const Iris::Component& component)
        {
            for (const auto& registration : registrations)
            {
                if (auto validated = ValidateRegistration(registration, component); !validated.Succeeded())
                {
                    return validated;
                }
            }
            return Succeeded();
        }

        Result ValidateAllEvents(const std::vector<EventRegistration>& registrations, const Iris::Component& component)
        {
            for (const auto& registration : registrations)
            {
                if (!registration.validate)
                {
                    continue;
                }
                if (auto validated = registration.validate(component); !validated.Succeeded())
                {
                    validated.diagnostic =
                        "Event '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
                    return validated;
                }
            }
            return Succeeded();
        }

        Result ValidateAllSharedStructs(const std::vector<SharedStructRegistration>& registrations,
                                        const Iris::Component& component)
        {
            for (const auto& registration : registrations)
            {
                if (auto validated = registration.validate(component); !validated.Succeeded())
                {
                    validated.diagnostic = "Shared struct '" + registration.name +
                                           "' does not match its C# struct: " + validated.diagnostic;
                    return validated;
                }
            }
            return Succeeded();
        }

        Result ValidateAllResources(const std::vector<ResourceRegistration>& registrations,
                                    const Iris::Component& component)
        {
            for (const auto& registration : registrations)
            {
                if (!registration.validate)
                {
                    continue;
                }
                if (auto validated = registration.validate(component); !validated.Succeeded())
                {
                    validated.diagnostic =
                        "Resource '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
                    return validated;
                }
            }
            return Succeeded();
        }

        // Size and alignment, and each field's name, type, kind and offset: two builds of a struct with the same
        // fingerprint lay the same bytes out the same way. A type the assembly does not expose (internal) can only be
        // told apart by its size.
        std::string FingerprintOf(const Iris::Component& component, const Description& description)
        {
            std::string text = std::to_string(description.size) + "/" + std::to_string(description.alignment);
            const auto type = component.FindType(description.name);
            if (!type.Succeeded())
            {
                return text + ";opaque";
            }
            const auto fields = type.value->GetFields();
            if (!fields.Succeeded())
            {
                return text + ";opaque";
            }
            for (const auto& field : *fields.value)
            {
                if (!field.isStatic)
                {
                    text += ";" + field.name + ":" + field.type.fullName + ":" +
                            std::to_string(static_cast<int>(field.kind)) + "@" +
                            (field.offset.has_value() ? std::to_string(*field.offset) : std::string("?"));
                }
            }
            return text;
        }

        // Removes every component of a script component type from every scene, and says how many there were.
        std::size_t ClearComponentData(App& app, const std::string& name, const OwnedLayout& layout)
        {
            const auto type = ComponentType::Dynamic(DynamicComponentType(name, layout.size, layout.alignment));
            std::size_t removed = 0;
            app.ForEachScene([&](Scene& scene) { removed += scene.GetEntityList().Clear(type); });
            return removed;
        }

        // What differs between the kinds of thing a script assembly defines. Everything else (refusing a name twice,
        // comparing with what the previous generation left, and keeping all-or-nothing) is the same for every kind and
        // lives in SchemaPass.
        struct SchemaKind
        {
            // Its name as it appears in messages, and which part of the script report it is.
            const char* noun;
            std::vector<Description> ScriptReport::* reported;
            // The rules this kind adds to "a name appears once": usable layouts, and names nobody else holds.
            std::function<Result(const std::vector<Description>&)> check;
            // Registers every definition or none. Undone by ComponentRegistry::RemoveScriptDefined.
            std::function<Result(const std::vector<Description>&)> registerAll;
            // Removes what the ECS or the App still holds for a definition that is gone (`gone`) or changed layout,
            // and says so.
            std::function<void(const std::string& name, const OwnedLayout& layout, bool gone)> dropStale;
            // Makes the storage a definition needs that is not yet owned, naming it in `owned` and `made` as soon as
            // it exists, so a failure further on can give it back. Null for a kind whose storage makes itself.
            std::function<Result(const Description&, OwnedLayouts& owned, std::vector<std::string>& made)> makeStorage;
            // Gives back storage `makeStorage` made. Null with it.
            std::function<void(const std::string& name)> removeStorage;
        };

        // One kind's definitions through one admission: discovered and checked, registered, compared with what is
        // still owned, given their storage, then recorded as owned.
        class SchemaPass
        {
          public:
            SchemaPass(SchemaKind kind, OwnedLayouts& owned) : m_kind(std::move(kind)), m_owned(owned)
            {
            }

            // Takes this kind's part of the script report. Then the checks, which change nothing.
            Result Discover(const ScriptReport& report)
            {
                const std::string noun = m_kind.noun;
                m_found = report.*(m_kind.reported);
                std::unordered_set<std::string> names;
                for (const auto& description : m_found)
                {
                    if (!names.insert(description.name).second)
                    {
                        return Failed("The script reported the " + noun + " '" + description.name +
                                      "' more than once.");
                    }
                }
                return m_kind.check(m_found);
            }

            // Registers the definitions, all or none.
            Result Register()
            {
                return m_kind.registerAll(m_found);
            }

            // Compares the definitions with what the previous generation left: data whose layout is unchanged stays,
            // anything else goes, with a warning, so no system ever reads old bytes as a new layout.
            void DropStale(ScriptAssembly& assembly)
            {
                for (const auto& description : m_found)
                {
                    m_next[description.name] = {assembly.Fingerprint(description), description.size,
                                                description.alignment};
                }
                for (auto at = m_owned.begin(); at != m_owned.end();)
                {
                    const auto next = m_next.find(at->first);
                    if (next != m_next.end() && next->second.fingerprint == at->second.fingerprint)
                    {
                        ++at;
                        continue;
                    }
                    m_kind.dropStale(at->first, at->second, next == m_next.end());
                    at = m_owned.erase(at);
                }
            }

            // Makes the storage of the definitions that have none yet.
            Result MakeStorage()
            {
                if (!m_kind.makeStorage)
                {
                    return Succeeded();
                }
                for (const auto& description : m_found)
                {
                    if (auto made = m_kind.makeStorage(description, m_owned, m_made); !made.Succeeded())
                    {
                        return made;
                    }
                }
                return Succeeded();
            }

            // Gives back the storage this admission made, so a refusal leaves none behind.
            void RemoveStorage()
            {
                for (const auto& name : m_made)
                {
                    m_kind.removeStorage(name);
                    m_owned.erase(name);
                }
                m_made.clear();
            }

            // Records the definitions as owned, with their fingerprints.
            void Commit()
            {
                for (auto& [name, layout] : m_next)
                {
                    m_owned[name] = std::move(layout);
                }
            }

          private:
            SchemaKind m_kind;
            OwnedLayouts& m_owned;
            std::vector<Description> m_found;
            OwnedLayouts m_next;
            std::vector<std::string> m_made;
        };

        SchemaKind ComponentKind(ComponentRegistry& registry, App& app)
        {
            SchemaKind kind;
            kind.noun = "component";
            kind.reported = &ScriptReport::components;
            kind.check = [&registry](const std::vector<Description>& found) {
                std::unordered_map<std::uint32_t, std::string> storageIds;
                for (const auto& description : found)
                {
                    if (!ToDynamicType(description).IsValid())
                    {
                        return Failed("The script component '" + description.name + "' reported a size of " +
                                      std::to_string(description.size) + " and an alignment of " +
                                      std::to_string(description.alignment) + ", which no C# struct has.");
                    }
                    if (registry.Find(description.name) >= 0)
                    {
                        return Failed("The script defines a component '" + description.name +
                                      "', but the engine already has a component of that name.");
                    }
                    const auto [at, added] = storageIds.emplace(ToDynamicType(description).Id(), description.name);
                    if (!added)
                    {
                        return Failed("The script components '" + at->second + "' and '" + description.name +
                                      "' map to the same storage; rename one.");
                    }
                }
                return Succeeded();
            };
            kind.registerAll = [&registry](const std::vector<Description>& found) {
                std::vector<ComponentRegistration> registrations;
                registrations.reserve(found.size());
                for (const auto& description : found)
                {
                    registrations.push_back(ComponentRegistration{
                        .name = description.name,
                        .typeName = description.name,
                        .type = ComponentType::Dynamic(ToDynamicType(description)),
                        .scriptDefined = true,
                        .validate = {}, // Its C# struct is the definition: there is no mapping to check it against.
                    });
                }
                // All or none: the checks make a refusal impossible, but if one happens nothing is half-registered.
                return registry.AddAll(std::move(registrations))
                           ? Succeeded()
                           : Failed("The script's components could not be registered together.");
            };
            kind.dropStale = [&app](const std::string& name, const OwnedLayout& layout, bool gone) {
                const std::size_t removed = ClearComponentData(app, name, layout);
                app.Logger()->Warn(
                    "Scripting: the script component '{}' {}; {} component(s) were removed so nothing reads the old "
                    "bytes as the new layout.",
                    name, gone ? "no longer exists" : "changed its layout", removed);
            };
            // A component's storage is made by the ECS when the first one is added, so there is none to make here.
            return kind;
        }

        SchemaKind EventKind(ComponentRegistry& registry, App& app, const OwnedLayouts& ownedEvents)
        {
            SchemaKind kind;
            kind.noun = "event";
            kind.reported = &ScriptReport::events;
            kind.check = [&registry, &app, &ownedEvents](const std::vector<Description>& found) {
                constexpr std::size_t maxAlignment = 16;
                for (const auto& description : found)
                {
                    const bool usable = description.size > 0 && description.alignment > 0 &&
                                        description.alignment <= maxAlignment &&
                                        (description.alignment & (description.alignment - 1)) == 0 &&
                                        description.size % description.alignment == 0;
                    if (!usable)
                    {
                        return Failed("The script event '" + description.name + "' reported a size of " +
                                      std::to_string(description.size) + " and an alignment of " +
                                      std::to_string(description.alignment) + ", which the engine cannot store.");
                    }
                    if (registry.FindEvent(description.name) >= 0 ||
                        (app.FindEventQueue(description.name) != nullptr && !ownedEvents.contains(description.name)))
                    {
                        return Failed("The script defines an event '" + description.name +
                                      "', but the engine already has an event of that name.");
                    }
                }
                return Succeeded();
            };
            kind.registerAll = [&registry](const std::vector<Description>& found) {
                std::vector<EventRegistration> registrations;
                for (const auto& description : found)
                {
                    EventRegistration registration;
                    registration.name = description.name;
                    registration.typeName = description.name;
                    registration.size = description.size;
                    registration.alignment = description.alignment;
                    registration.scriptDefined = true;
                    registrations.push_back(std::move(registration));
                }
                return registry.AddEvents(std::move(registrations))
                           ? Succeeded()
                           : Failed("The script's events could not be registered together.");
            };
            kind.dropStale = [&app](const std::string& name, const OwnedLayout&, bool gone) {
                (void)app.RemoveEventQueue(name);
                app.Logger()->Warn("Scripting: the script event '{}' {}; its queue and the events in it were dropped.",
                                   name, gone ? "no longer exists" : "changed its layout");
            };
            kind.makeStorage = [&app](const Description& description, OwnedLayouts& owned,
                                      std::vector<std::string>& made) {
                // A queue whose layout is unchanged is kept, with the events in it.
                if (owned.contains(description.name))
                {
                    return Succeeded();
                }
                if (app.CreateEventQueue(description.name, description.size, description.alignment) == nullptr)
                {
                    return Failed("The queue for the script event '" + description.name + "' could not be made.");
                }
                owned[description.name] = {{}, description.size, description.alignment};
                made.push_back(description.name);
                return Succeeded();
            };
            kind.removeStorage = [&app](const std::string& name) { (void)app.RemoveEventQueue(name); };
            return kind;
        }
    } // namespace

    Result ValidateRegistration(const ComponentRegistration& registration, const Iris::Component& component)
    {
        if (!registration.validate)
        {
            return Succeeded();
        }
        auto validated = registration.validate(component);
        if (!validated.Succeeded())
        {
            validated.diagnostic =
                "Component '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
        }
        return validated;
    }

    IrisScriptAssembly::IrisScriptAssembly(EngineContext& context, const Iris::Component& component)
        : m_context(context), m_component(component)
    {
    }

    Result IrisScriptAssembly::Report(ScriptReport& report)
    {
        struct Table
        {
            const char* noun;
            const char* type;
        };
        constexpr Table tables[] = {{"component", "Fenrir.Scripting.Generated.ComponentTable"},
                                    {"event", "Fenrir.Scripting.Generated.EventTable"}};
        for (const auto& [noun, type] : tables)
        {
            const auto table = m_component.FindType(type);
            if (table.status == Iris::CallStatus::NotFound)
            {
                continue;
            }
            if (!table.Succeeded())
            {
                return Result{table.status,
                              std::string("The ") + noun + " table could not be read: " + table.diagnostic, false};
            }
            const auto run = table.value->FindMethod<void()>("Register");
            if (!run.Succeeded())
            {
                return Result{run.status,
                              std::string("The ") + noun + " table has no Register method: " + run.diagnostic, false};
            }
            // The table's Register calls the engine's natives, which put what it reports where the slot points.
            m_context.reporting = &report;
            const auto ran = run.value->Invoke();
            m_context.reporting = nullptr;
            if (!ran.Succeeded())
            {
                return Result{ran.status,
                              std::string("The script assembly could not report its ") + noun + "s: " + ran.diagnostic,
                              ran.diagnosticTruncated};
            }
        }
        return Succeeded();
    }

    std::string IrisScriptAssembly::Fingerprint(const Description& description)
    {
        return FingerprintOf(m_component, description);
    }

    Result IrisScriptAssembly::CheckNativeAgainstCSharp(const ComponentRegistry& registry)
    {
        if (auto validated = ValidateAll(registry.All(), m_component); !validated.Succeeded())
        {
            return validated;
        }
        if (auto validated = ValidateAllEvents(registry.Events(), m_component); !validated.Succeeded())
        {
            return validated;
        }
        if (auto validated = ValidateAllSharedStructs(registry.SharedStructs(), m_component); !validated.Succeeded())
        {
            return validated;
        }
        return ValidateAllResources(registry.Resources(), m_component);
    }

    // Admission, in an order that puts the one irreversible step as late as it can go and undoes everything after it:
    //   1. discover what the script defines, kind by kind, and check it;
    //   2. check every native component's, event's, shared struct's and resource's mapping against its C# struct;
    //   3. register what the script defines (undone by Forget);
    //   4. drop what the previous generation left that no longer matches (irreversible);
    //   5. make the queues of new events (undone by removing them);
    //   6. record the definitions as owned.
    // So a refusal leaves the registry as Forget would, and the data either untouched or as the drop left it: the
    // state a restore of the previous generation admits against.
    Result ScriptData::Admit(App& app, ScriptAssembly& assembly)
    {
        ScriptReport report;
        if (auto reported = assembly.Report(report); !reported.Succeeded())
        {
            return reported;
        }
        std::vector<SchemaPass> passes;
        passes.emplace_back(ComponentKind(m_registry, app), m_components);
        passes.emplace_back(EventKind(m_registry, app, m_events), m_events);

        for (auto& pass : passes)
        {
            if (auto discovered = pass.Discover(report); !discovered.Succeeded())
            {
                return discovered;
            }
        }
        if (auto validated = assembly.CheckNativeAgainstCSharp(m_registry); !validated.Succeeded())
        {
            return validated;
        }
        for (auto& pass : passes)
        {
            if (auto registered = pass.Register(); !registered.Succeeded())
            {
                Forget();
                return registered;
            }
        }
        // Everything checks out and is registered, so from here the new generation is kept.
        for (auto& pass : passes)
        {
            pass.DropStale(assembly);
        }
        for (auto& pass : passes)
        {
            if (auto made = pass.MakeStorage(); !made.Succeeded())
            {
                Forget();
                for (auto& undo : passes)
                {
                    undo.RemoveStorage();
                }
                return made;
            }
        }
        for (auto& pass : passes)
        {
            pass.Commit();
        }
        return Succeeded();
    }

    void ScriptData::Forget()
    {
        m_registry.RemoveScriptDefined();
    }

    void ScriptData::Drop(App& app)
    {
        for (const auto& [name, layout] : m_components)
        {
            (void)ClearComponentData(app, name, layout);
        }
        for (const auto& [name, layout] : m_events)
        {
            (void)app.RemoveEventQueue(name);
        }
        m_components.clear();
        m_events.clear();
        Forget();
    }
} // namespace Fenrir::Detail
