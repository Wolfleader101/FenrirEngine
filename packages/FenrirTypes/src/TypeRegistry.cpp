#include "FenrirTypes/TypeRegistry.hpp"

#include <string>
#include <utility>

namespace Fenrir
{
    namespace
    {
        std::string Quoted(std::string_view text)
        {
            return "`" + std::string(text) + "`";
        }

        // The type and its package, as a diagnostic names it: `Fenrir::Transform` (FenrirECS).
        std::string Named(const TypeDescription& description)
        {
            return Quoted(description.name) + " (" + std::string(description.package) + ")";
        }

        // The first owning kind in `type` or its elements, the field's own kind first; null when there is none.
        const ValueType* FirstOwning(const ValueType& type) noexcept
        {
            if (IsOwning(type.kind))
                return &type;
            if (type.element != nullptr)
                return FirstOwning(*type.element);
            return nullptr;
        }
    } // namespace

    TypeRegistry::TypeRegistry(Diagnostics diagnostics) : m_diagnostics(std::move(diagnostics))
    {
    }

    TypeRegistry::~TypeRegistry() = default;
    TypeRegistry::TypeRegistry(TypeRegistry&&) noexcept = default;
    TypeRegistry& TypeRegistry::operator=(TypeRegistry&&) noexcept = default;

    bool TypeRegistry::Register(const TypeDescription& description)
    {
        const std::string prefix = "cannot register " + Named(description) + ": ";

        if (description.version < 1)
        {
            Report(prefix + "its version is " + std::to_string(description.version) + ", and versions start at 1");
            return false;
        }

        // Every name it would take, canonical first, each checked against every registered type's names.
        std::vector<std::string_view> names{description.name};
        names.insert(names.end(), description.formerly.begin(), description.formerly.end());
        for (std::string_view name : names)
        {
            if (const Entry* holder = FindEntry(name))
            {
                Report(prefix + "the name " + Quoted(name) + " is already " + Named(*holder->description) + "'s");
                return false;
            }
        }

        if (!description.base.empty())
        {
            if (description.role != Role::DataAsset)
            {
                Report(prefix + "it extends " + Quoted(description.base) +
                       ", and only a data asset type may have a base, not a " +
                       std::string(RoleName(description.role)));
                return false;
            }
            const TypeDescription* base = Find(description.base);
            if (base == nullptr)
            {
                Report(prefix + "its base " + Quoted(description.base) + " is not registered; register the base first");
                return false;
            }
            if (base->role != Role::DataAsset)
            {
                Report(prefix + "its base " + Named(*base) + " is a " + std::string(RoleName(base->role)) +
                       ", and a data asset type extends only another data asset type");
                return false;
            }
        }

        if (!AdmitsOwningKinds(description.role))
        {
            for (const FieldInfo& field : description.fields)
            {
                if (const ValueType* owning = FirstOwning(field.type))
                {
                    Report(prefix + "its field " + Quoted(field.name) + " holds a " +
                           std::string(KindName(owning->kind)) + ", which a " +
                           std::string(RoleName(description.role)) +
                           " may not; only data asset and settings types hold owning kinds");
                    return false;
                }
            }
        }

        const std::size_t index = m_entries.size();
        auto entry = std::make_unique<Entry>();
        entry->description = &description;
        m_entries.push_back(std::move(entry));
        for (std::string_view name : names)
            m_names.emplace(std::string(name), index);
        return true;
    }

    const TypeDescription* TypeRegistry::Find(std::string_view name) const noexcept
    {
        const Entry* entry = FindEntry(name);
        return entry != nullptr ? entry->description : nullptr;
    }

    std::vector<const TypeDescription*> TypeRegistry::Types(Role role) const
    {
        std::vector<const TypeDescription*> types;
        for (const auto& entry : m_entries)
            if (entry->description->role == role)
                types.push_back(entry->description);
        return types;
    }

    bool TypeRegistry::IsA(std::string_view type, std::string_view base) const noexcept
    {
        const TypeDescription* target = Find(base);
        if (target == nullptr)
            return false;
        // Registration refuses a base that is not yet registered, so the chain ends and has no cycle.
        for (const TypeDescription* step = Find(type); step != nullptr;
             step = step->base.empty() ? nullptr : Find(step->base))
        {
            if (step == target)
                return true;
        }
        return false;
    }

    bool TypeRegistry::AttachSlot(std::string_view name, std::type_index tag, std::unique_ptr<SlotBase> slot)
    {
        const Entry* found = FindEntry(name);
        if (found == nullptr)
        {
            Report("cannot attach to " + Quoted(name) + ": no type is registered under that name");
            return false;
        }
        // FindEntry hands out const entries; this registry owns them, and is not const here.
        const_cast<Entry*>(found)->attachments[tag] = std::move(slot);
        return true;
    }

    TypeRegistry::SlotBase* TypeRegistry::FindSlot(std::string_view name, std::type_index tag) const noexcept
    {
        const Entry* entry = FindEntry(name);
        if (entry == nullptr)
            return nullptr;
        const auto found = entry->attachments.find(tag);
        return found != entry->attachments.end() ? found->second.get() : nullptr;
    }

    const TypeRegistry::Entry* TypeRegistry::FindEntry(std::string_view name) const noexcept
    {
        const auto found = m_names.find(name);
        return found != m_names.end() ? m_entries[found->second].get() : nullptr;
    }

    void TypeRegistry::Report(const std::string& message) const
    {
        if (m_diagnostics)
            m_diagnostics(message);
    }
} // namespace Fenrir
