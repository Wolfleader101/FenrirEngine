#include "ScriptSchema.hpp"

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

        // The script assembly as a loaded Iris component: it reports what it defines by running the tables the SDK's
        // generator wrote into it, reads its types for fingerprints, and holds the C# structs the native components are
        // checked against.
        class IrisScriptAssembly final : public ScriptAssembly
        {
          public:
            IrisScriptAssembly(EngineContext& context, const Iris::Component& component)
                : m_context(context), m_component(component)
            {
            }

            Result Report(ScriptKind kind, std::vector<Description>& found) override
            {
                const bool components = kind == ScriptKind::Component;
                const std::string noun = components ? "component" : "event";
                const char* tableType =
                    components ? "Fenrir.Scripting.Generated.ComponentTable" : "Fenrir.Scripting.Generated.EventTable";
                auto EngineContext::* slot =
                    components ? &EngineContext::discovering : &EngineContext::discoveringEvents;

                const auto table = m_component.FindType(tableType);
                if (table.status == Iris::CallStatus::NotFound)
                {
                    return Succeeded();
                }
                if (!table.Succeeded())
                {
                    return Result{table.status, "The " + noun + " table could not be read: " + table.diagnostic, false};
                }
                const auto report = table.value->FindMethod<void()>("Register");
                if (!report.Succeeded())
                {
                    return Result{report.status, "The " + noun + " table has no Register method: " + report.diagnostic,
                                  false};
                }
                m_context.*slot = &found;
                const auto ran = report.value->Invoke();
                m_context.*slot = nullptr;
                if (!ran.Succeeded())
                {
                    return Result{ran.status,
                                  "The script assembly could not report its " + noun + "s: " + ran.diagnostic,
                                  ran.diagnosticTruncated};
                }
                return Succeeded();
            }

            std::string Fingerprint(const Description& description) override
            {
                return FingerprintOf(m_component, description);
            }

            Result CheckNativeAgainstCSharp(const ComponentRegistry& registry) override
            {
                if (auto validated = ValidateAll(registry.All(), m_component); !validated.Succeeded())
                {
                    return validated;
                }
                if (auto validated = ValidateAllEvents(registry.Events(), m_component); !validated.Succeeded())
                {
                    return validated;
                }
                return ValidateAllSharedStructs(registry.SharedStructs(), m_component);
            }

          private:
            EngineContext& m_context;
            const Iris::Component& m_component;
        };

        // Removes every component of a script component type from every scene, and says how many there were.
        std::size_t ClearComponentData(App& app, const std::string& name, const OwnedLayout& layout)
        {
            const auto type = ComponentType::Dynamic(DynamicComponentType(name, layout.size, layout.alignment));
            std::size_t removed = 0;
            app.ForEachScene([&](Scene& scene) { removed += scene.GetEntityList().Clear(type); });
            return removed;
        }

        // What differs between the kinds of thing a script assembly defines. Everything else (finding what the
        // assembly defines, refusing a name twice, comparing with what the previous generation left, and keeping
        // all-or-nothing) is the same for every kind and lives in SchemaPass.
        struct SchemaKind
        {
            // Which kind this is, and its name as it appears in messages.
            ScriptKind which;
            const char* noun;
            // The rules this kind adds to "a name appears once": usable layouts, and names nobody else holds.
            std::function<Result(const std::vector<Description>&)> check;
            // Removes what the ECS or the App still holds for a definition that is gone (`gone`) or changed layout,
            // and says so.
            std::function<void(const std::string& name, const OwnedLayout& layout, bool gone)> dropStale;
            // Registers every definition or none, and makes whatever storage they need. `owned` must name any storage
            // as soon as it exists, so a failure further on never leaves storage nobody owns.
            std::function<Result(const std::vector<Description>&, OwnedLayouts& owned)> keep;
        };

        // One kind's definitions through one load: discovered and checked, compared with what is still owned, kept.
        class SchemaPass
        {
          public:
            SchemaPass(SchemaKind kind, OwnedLayouts& owned) : m_kind(std::move(kind)), m_owned(owned)
            {
            }

            // Asks the script assembly what it defines. Then the checks, which change nothing.
            Result Discover(ScriptAssembly& assembly)
            {
                const std::string noun = m_kind.noun;
                m_found.clear();
                if (auto reported = assembly.Report(m_kind.which, m_found); !reported.Succeeded())
                {
                    return reported;
                }
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

            // Registers the definitions and records them as owned.
            Result Keep()
            {
                if (auto kept = m_kind.keep(m_found, m_owned); !kept.Succeeded())
                {
                    return kept;
                }
                for (auto& [name, layout] : m_next)
                {
                    m_owned[name] = std::move(layout);
                }
                return Succeeded();
            }

          private:
            SchemaKind m_kind;
            OwnedLayouts& m_owned;
            std::vector<Description> m_found;
            OwnedLayouts m_next;
        };

        SchemaKind ComponentKind(ComponentRegistry& registry, App& app)
        {
            SchemaKind kind;
            kind.which = ScriptKind::Component;
            kind.noun = "component";
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
            kind.dropStale = [&app](const std::string& name, const OwnedLayout& layout, bool gone) {
                const std::size_t removed = ClearComponentData(app, name, layout);
                app.Logger()->Warn(
                    "Scripting: the script component '{}' {}; {} component(s) were removed so nothing reads the old "
                    "bytes as the new layout.",
                    name, gone ? "no longer exists" : "changed its layout", removed);
            };
            kind.keep = [&registry](const std::vector<Description>& found, OwnedLayouts&) {
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
            return kind;
        }

        SchemaKind EventKind(ComponentRegistry& registry, App& app, const OwnedLayouts& ownedEvents)
        {
            SchemaKind kind;
            kind.which = ScriptKind::Event;
            kind.noun = "event";
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
            kind.dropStale = [&app](const std::string& name, const OwnedLayout&, bool gone) {
                (void)app.RemoveEventQueue(name);
                app.Logger()->Warn("Scripting: the script event '{}' {}; its queue and the events in it were dropped.",
                                   name, gone ? "no longer exists" : "changed its layout");
            };
            kind.keep = [&registry, &app](const std::vector<Description>& found, OwnedLayouts& owned) {
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
                if (!registry.AddEvents(std::move(registrations)))
                {
                    return Failed("The script's events could not be registered together.");
                }
                for (const auto& description : found)
                {
                    // A queue whose layout is unchanged is kept, with the events in it.
                    if (!owned.contains(description.name))
                    {
                        if (app.CreateEventQueue(description.name, description.size, description.alignment) == nullptr)
                        {
                            return Failed("The queue for the script event '" + description.name +
                                          "' could not be made.");
                        }
                        owned[description.name] = {{}, description.size, description.alignment};
                    }
                }
                return Succeeded();
            };
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

    // Nothing is registered unless every check passes, so a failure leaves the registry exactly as it was:
    //   1. discover what the script defines, kind by kind, and check it;
    //   2. check every native component's, event's and shared struct's mapping against its C# struct;
    //   3. drop what the previous generation left that no longer matches;
    //   4. only then register what the script defines.
    Result Reconcile(App& app, ComponentRegistry& registry, ScriptAssembly& assembly, OwnedScriptData& owned)
    {
        std::vector<SchemaPass> passes;
        passes.emplace_back(ComponentKind(registry, app), owned.components);
        passes.emplace_back(EventKind(registry, app, owned.events), owned.events);

        for (auto& pass : passes)
        {
            if (auto discovered = pass.Discover(assembly); !discovered.Succeeded())
            {
                return discovered;
            }
        }
        if (auto validated = assembly.CheckNativeAgainstCSharp(registry); !validated.Succeeded())
        {
            return validated;
        }
        // Everything checks out, so from here the new generation is kept.
        for (auto& pass : passes)
        {
            pass.DropStale(assembly);
        }
        for (auto& pass : passes)
        {
            if (auto kept = pass.Keep(); !kept.Succeeded())
            {
                return kept;
            }
        }
        return Succeeded();
    }

    Result Reconcile(EngineContext& context, ComponentRegistry& registry, const Iris::Component& component,
                     OwnedScriptData& owned)
    {
        IrisScriptAssembly assembly(context, component);
        return Reconcile(*context.app, registry, assembly, owned);
    }

    void DropScriptData(App& app, OwnedScriptData& owned)
    {
        for (const auto& [name, layout] : owned.components)
        {
            (void)ClearComponentData(app, name, layout);
        }
        for (const auto& [name, layout] : owned.events)
        {
            (void)app.RemoveEventQueue(name);
        }
        owned.components.clear();
        owned.events.clear();
    }
} // namespace Fenrir::Detail
