#include "ScriptTypes.hpp"

#include "FenrirECS/TypeAttachments.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Fenrir::Detail
{
    namespace
    {
        using Result = Iris::CallResult<void>;

        Result Succeeded()
        {
            return Result{Iris::CallStatus::Success, {}, false};
        }

        Result Failed(std::string diagnostic)
        {
            return Result{Iris::CallStatus::InvalidArgument, std::move(diagnostic), false};
        }

        // The defaults each slot's constructor copies, set while a description that holds the slot lives. Atomic, as a
        // constructor may run on any thread that reads a description, though only the main thread admits.
        struct Slots
        {
            std::mutex mutex;
            std::array<std::atomic<const std::vector<std::byte>*>, ScriptTypes::SlotCount> defaults{};
        };

        Slots& TheSlots()
        {
            static Slots slots;
            return slots;
        }

        // A description's `construct`: copies slot I's defaults, the bytes of the type's `new T()`.
        template <std::size_t I>
        void ConstructFromSlot(void* instance)
        {
            if (const std::vector<std::byte>* defaults = TheSlots().defaults[I].load(std::memory_order_acquire))
            {
                std::memcpy(instance, defaults->data(), defaults->size());
            }
        }

        template <std::size_t... I>
        constexpr std::array<void (*)(void*), sizeof...(I)> MakeConstructors(std::index_sequence<I...>)
        {
            return {&ConstructFromSlot<I>...};
        }

        constexpr auto Constructors = MakeConstructors(std::make_index_sequence<ScriptTypes::SlotCount>{});

        // A slot taken for `defaults`, or none when every slot is taken.
        std::optional<std::size_t> AcquireSlot(const std::vector<std::byte>& defaults)
        {
            Slots& slots = TheSlots();
            const std::lock_guard<std::mutex> lock(slots.mutex);
            for (std::size_t slot = 0; slot < slots.defaults.size(); ++slot)
            {
                if (slots.defaults[slot].load(std::memory_order_relaxed) == nullptr)
                {
                    slots.defaults[slot].store(&defaults, std::memory_order_release);
                    return slot;
                }
            }
            return std::nullopt;
        }

        void ReleaseSlot(std::size_t slot)
        {
            Slots& slots = TheSlots();
            const std::lock_guard<std::mutex> lock(slots.mutex);
            slots.defaults[slot].store(nullptr, std::memory_order_release);
        }

        // Whether a script field may be of this kind: fixed-layout, and nothing a script never reports.
        bool Reportable(Kind kind)
        {
            return !IsOwning(kind) && kind != Kind::Enum;
        }
    } // namespace

    struct ScriptTypes::Entry
    {
        // The report's own copy: the description's names, fields' names and targets are views of it.
        ScriptComponentDescription reported;
        bool component = false;
        TypeDescription description;
        std::vector<FieldInfo> fields;
        // The element types of array fields, which a field's ValueType points at, so they never move.
        std::deque<ValueType> elements;
        std::vector<std::byte> defaults;
        std::optional<std::size_t> slot;
        // Whether the registry holds the description now.
        bool registered = false;

        ~Entry()
        {
            if (slot)
            {
                ReleaseSlot(*slot);
            }
        }
    };

    ScriptTypes::ScriptTypes() = default;

    ScriptTypes::~ScriptTypes() = default;

    Result ScriptTypes::Build(const ScriptReport& report, const TypeRegistry& types)
    {
        m_entries.clear();

        // Every struct and component gets an entry first, so a field can point at the description of a struct the
        // report holds whatever order the report gives them in.
        // By name: a field may hold a struct or another component by value.
        std::unordered_map<std::string, Entry*> byName;
        std::vector<std::unique_ptr<Entry>> made;
        std::unordered_set<std::string> names;
        const auto add = [&](const ScriptComponentDescription& reported, bool component) -> Result {
            const char* noun = component ? "component" : "struct";
            if (!names.insert(reported.name).second)
            {
                return Failed("The script reported the " + std::string(noun) + " '" + reported.name +
                              "' more than once, or as a component and a struct.");
            }
            if (const TypeDescription* held = types.Find(reported.name))
            {
                return Failed("The script defines a " + std::string(noun) + " '" + reported.name +
                              "', but the engine already has a type of that name, from " + std::string(held->package) +
                              ".");
            }
            if (!reported.defaults.empty() && reported.defaults.size() != reported.size)
            {
                return Failed("The script " + std::string(noun) + " '" + reported.name + "' reported " +
                              std::to_string(reported.defaults.size()) + " bytes of defaults for a size of " +
                              std::to_string(reported.size) + ".");
            }
            auto entry = std::make_unique<Entry>();
            entry->reported = reported;
            entry->component = component;
            entry->defaults = reported.defaults.empty() ? std::vector<std::byte>(reported.size) : reported.defaults;
            byName.emplace(reported.name, entry.get());
            made.push_back(std::move(entry));
            return Succeeded();
        };
        for (const auto& reported : report.structs)
        {
            if (auto added = add(reported, false); !added.Succeeded())
            {
                return added;
            }
        }
        for (const auto& reported : report.components)
        {
            if (auto added = add(reported, true); !added.Succeeded())
            {
                return added;
            }
        }

        // The value type of one field, or of an array's elements, resolving the struct it names.
        const auto valueOf = [&](const Entry& owner, const ScriptFieldDescription& field, Kind kind,
                                 ValueType& type) -> Result {
            const std::string where = "The field '" + field.name + "' of the script type '" + owner.reported.name + "'";
            if (!Reportable(kind))
            {
                return Failed(where + " is of kind " + std::string(KindName(kind)) +
                              ", which a component cannot hold.");
            }
            type.kind = kind;
            switch (kind)
            {
            case Kind::Struct:
                if (const auto found = byName.find(field.target); found != byName.end())
                {
                    type.type = &found->second->description;
                }
                else if (const TypeDescription* registered = types.Find(field.target);
                         registered != nullptr && !registered->treeBacked)
                {
                    type.type = registered;
                }
                else
                {
                    return Failed(where + " holds the struct '" + field.target +
                                  "', which neither the script nor the engine describes.");
                }
                break;
            case Kind::Reference:
                type.target = field.target;
                break;
            case Kind::FixedString:
                type.count = field.count;
                break;
            default:
                break;
            }
            return Succeeded();
        };

        for (auto& entry : made)
        {
            // Sizes first: a struct's size is what a field holding it takes.
            entry->description.name = entry->reported.name;
            entry->description.size = entry->reported.size;
            entry->description.alignment = entry->reported.alignment;
        }
        for (auto& entry : made)
        {
            for (const auto& field : entry->reported.fields)
            {
                FieldInfo info;
                info.name = field.name;
                info.offset = field.offset;
                info.transient = field.transient;
                if (field.kind == Kind::Array)
                {
                    if (field.elementKind == Kind::Array || field.elementKind == Kind::FixedString || field.count == 0)
                    {
                        return Failed("The field '" + field.name + "' of the script type '" + entry->reported.name +
                                      "' is an array the engine cannot describe.");
                    }
                    ValueType& element = entry->elements.emplace_back();
                    if (auto resolved = valueOf(*entry, field, field.elementKind, element); !resolved.Succeeded())
                    {
                        return resolved;
                    }
                    info.type.kind = Kind::Array;
                    info.type.element = &element;
                    info.type.count = field.count;
                }
                else if (auto resolved = valueOf(*entry, field, field.kind, info.type); !resolved.Succeeded())
                {
                    return resolved;
                }
                const std::size_t size = SizeOf(info.type);
                if (size != field.size)
                {
                    return Failed("The field '" + field.name + "' of the script type '" + entry->reported.name +
                                  "' takes " + std::to_string(field.size) + " bytes, but its kind, " +
                                  std::string(KindName(info.type.kind)) + ", takes " + std::to_string(size) + ".");
                }
                if (field.offset + field.size > entry->reported.size)
                {
                    return Failed("The field '" + field.name + "' of the script type '" + entry->reported.name +
                                  "' ends past the type's " + std::to_string(entry->reported.size) + " bytes.");
                }
                entry->fields.push_back(info);
            }
        }

        // The order they register in: each struct after the structs its fields hold, then the components.
        std::vector<std::unique_ptr<Entry>> ordered;
        std::unordered_set<const Entry*> placed;
        std::unordered_map<const TypeDescription*, std::unique_ptr<Entry>*> byDescription;
        for (auto& entry : made)
        {
            byDescription.emplace(&entry->description, &entry);
        }
        const std::function<void(std::unique_ptr<Entry>&)> place = [&](std::unique_ptr<Entry>& entry) {
            if (entry == nullptr || placed.contains(entry.get()))
            {
                return;
            }
            placed.insert(entry.get());
            for (const FieldInfo& field : entry->fields)
            {
                const ValueType& held = field.type.kind == Kind::Array ? *field.type.element : field.type;
                if (held.kind == Kind::Struct)
                {
                    if (const auto found = byDescription.find(held.type); found != byDescription.end())
                    {
                        place(*found->second);
                    }
                }
            }
            ordered.push_back(std::move(entry));
        };
        for (auto& entry : made)
        {
            if (entry != nullptr && !entry->component)
            {
                place(entry);
            }
        }
        for (auto& entry : made)
        {
            if (entry != nullptr)
            {
                place(entry);
            }
        }

        for (auto& entry : ordered)
        {
            entry->slot = AcquireSlot(entry->defaults);
            if (!entry->slot)
            {
                return Failed("The script defines more than " + std::to_string(SlotCount) +
                              " component and struct types, more than the engine can describe at once.");
            }
            TypeDescription& description = entry->description;
            description.package = "FenrirScripting";
            description.role = entry->component ? Role::Component : Role::Nested;
            description.transient = entry->reported.transient;
            description.editorOnly = entry->reported.editorOnly;
            description.construct = Constructors[*entry->slot];
            description.fields = entry->fields;
        }
        m_entries = std::move(ordered);
        return Succeeded();
    }

    Result ScriptTypes::Register(TypeRegistry& types, const ComponentTypeOf& componentTypeOf)
    {
        for (const auto& entry : m_entries)
        {
            const std::string name = entry->reported.name;
            if (!types.Register(entry->description))
            {
                (void)Unregister(types);
                return Failed("The type registry refused the script type '" + name + "'.");
            }
            entry->registered = true;
            if (!entry->component)
            {
                continue;
            }
            const std::optional<ComponentType> component = componentTypeOf(name);
            if (!component || !types.Attach<ComponentTypeAttachment>(name, *component))
            {
                (void)Unregister(types);
                return Failed("The script component '" + name + "' could not be given its component type.");
            }
        }
        return Succeeded();
    }

    bool ScriptTypes::Unregister(TypeRegistry& types)
    {
        bool all = true;
        for (auto entry = m_entries.rbegin(); entry != m_entries.rend(); ++entry)
        {
            if ((*entry)->registered)
            {
                (*entry)->registered = !types.Unregister((*entry)->reported.name);
                all = all && !(*entry)->registered;
            }
        }
        return all;
    }
} // namespace Fenrir::Detail
