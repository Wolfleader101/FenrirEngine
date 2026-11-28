#include "FenrirTypes/TypeRegistry.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

        // The first owning kind in `type` or along its elements, the field's own kind first; null when there is none.
        // A value of a nested struct is its own description's business, so this stops at one.
        const ValueType* OwnOwning(const ValueType& type) noexcept
        {
            if (IsOwning(type.kind))
                return &type;
            if (type.element != nullptr)
                return OwnOwning(*type.element);
            return nullptr;
        }

        // The first owning kind `type` holds, through its elements and nested structs' fields; null when none.
        // Structs held by value nest finitely, so the walk ends.
        const ValueType* HeldOwning(const ValueType& type) noexcept
        {
            if (const ValueType* owning = OwnOwning(type))
                return owning;
            const ValueType* leaf = &type;
            while (leaf->element != nullptr)
                leaf = leaf->element;
            if (leaf->kind == Kind::Struct && leaf->type != nullptr)
            {
                for (const FieldInfo& field : leaf->type->fields)
                    if (const ValueType* owning = HeldOwning(field.type))
                        return owning;
                for (const Alternative& alternative : leaf->type->alternatives)
                    if (const ValueType* owning = HeldOwning(alternative.field.type))
                        return owning;
            }
            return nullptr;
        }

        // The first tree-backed struct `type` holds, through its elements and nested structs' fields; null when none.
        // A list may hold its own struct (`std::vector<Self>`), so `seen` stops the walk at a struct it has walked.
        const TypeDescription* HeldTreeBacked(const ValueType& type, std::vector<const TypeDescription*>& seen)
        {
            const ValueType* leaf = &type;
            while (leaf->element != nullptr)
                leaf = leaf->element;
            if (leaf->kind != Kind::Struct || leaf->type == nullptr)
                return nullptr;
            if (leaf->type->treeBacked)
                return leaf->type;
            if (std::find(seen.begin(), seen.end(), leaf->type) != seen.end())
                return nullptr;
            seen.push_back(leaf->type);
            for (const FieldInfo& field : leaf->type->fields)
                if (const TypeDescription* held = HeldTreeBacked(field.type, seen))
                    return held;
            for (const Alternative& alternative : leaf->type->alternatives)
                if (const TypeDescription* held = HeldTreeBacked(alternative.field.type, seen))
                    return held;
            return nullptr;
        }

        const TypeDescription* HeldTreeBacked(const ValueType& type)
        {
            std::vector<const TypeDescription*> seen;
            return HeldTreeBacked(type, seen);
        }

        // Whether `type`, through its elements, is a struct of `description`: a field holding a value of it.
        bool Holds(const ValueType& type, const TypeDescription& description) noexcept
        {
            const ValueType* leaf = &type;
            while (leaf->element != nullptr)
                leaf = leaf->element;
            return leaf->kind == Kind::Struct && leaf->type == &description;
        }

        // Why the tree-backed `description` cannot be registered, as the rest of a diagnostic; empty when it can.
        std::string TreeBackedRefusal(const TypeDescription& description)
        {
            if (description.role != Role::DataAsset && description.role != Role::Nested)
                return "it is tree-backed and a " + std::string(RoleName(description.role)) +
                       ", and only a data asset or nested type may be tree-backed";
            if (description.size != 0 || description.construct != nullptr || description.destroy != nullptr ||
                description.visit != nullptr)
                return "it is tree-backed, so has no layout, yet it has a size, construct, destroy or visit function";
            for (const FieldInfo& field : description.fields)
                if (field.offset != 0)
                    return "it is tree-backed, so its fields have no offsets, yet its field " + Quoted(field.name) +
                           " has offset " + std::to_string(field.offset);
            if (!description.alternatives.empty() || !description.enumerators.empty())
                return "it is tree-backed, and a tree-backed type is neither a tagged union nor an enum";
            if (description.defaultTree.empty())
                return "it is tree-backed and has no default tree";
            return {};
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

        // Every name it would take, canonical first, each checked against every registered type's names and its own.
        std::vector<std::string_view> names{description.name};
        names.insert(names.end(), description.formerly.begin(), description.formerly.end());
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (const Entry* holder = FindEntry(names[i]))
            {
                Report(prefix + "the name " + Quoted(names[i]) + " is already " + Named(*holder->description) + "'s");
                return false;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (names[j] == names[i])
                {
                    Report(prefix + "it names " + Quoted(names[i]) + " twice among its canonical and former names");
                    return false;
                }
            }
        }

        if (description.treeBacked)
        {
            if (const std::string refusal = TreeBackedRefusal(description); !refusal.empty())
            {
                Report(prefix + refusal);
                return false;
            }
        }
        else if (!description.defaultTree.empty())
        {
            Report(prefix + "it has a default tree and is not tree-backed; its default is an instance it constructs");
            return false;
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
            if (base->treeBacked && !description.treeBacked)
            {
                Report(
                    prefix + "its base " + Named(*base) +
                    " is tree-backed, so has no layout for a C++ type to extend; only a tree-backed type extends one");
                return false;
            }
        }

        for (const FieldInfo& field : description.fields)
        {
            // A layout cannot hold a value whose layout C++ does not know.
            if (!description.treeBacked)
            {
                if (const TypeDescription* held = HeldTreeBacked(field.type))
                {
                    Report(prefix + "its field " + Quoted(field.name) + " holds " + Named(*held) +
                           ", which is tree-backed, so has no layout to hold it by");
                    return false;
                }
            }
            // A nested struct's owning kinds are allowed or refused in the role of the type that holds it.
            const ValueType* held = HeldOwning(field.type);
            if (held != nullptr && description.role != Role::Nested && !AdmitsOwningKinds(description.role))
            {
                Report(prefix + "its field " + Quoted(field.name) + " holds a " + std::string(KindName(held->kind)) +
                       ", which a " + std::string(RoleName(description.role)) +
                       " may not; only data asset and settings types hold owning kinds");
                return false;
            }
            // An owning field has no offset to visit it by, so the type must bring its own visit function; a
            // tree-backed type's fields are only ever walked in its tree.
            if (OwnOwning(field.type) != nullptr && description.visit == nullptr && !description.treeBacked)
            {
                Report(prefix + "its field " + Quoted(field.name) + " holds a " +
                       std::string(KindName(OwnOwning(field.type)->kind)) +
                       ", which has no offset to visit it by, and the type has no visit function");
                return false;
            }
        }

        // An alternative's value lives in the union's bytes, read at the union's offset, so it is fixed-layout.
        for (const Alternative& alternative : description.alternatives)
        {
            if (const ValueType* held = HeldOwning(alternative.field.type))
            {
                Report(prefix + "its alternative " + Quoted(alternative.field.name) + " holds a " +
                       std::string(KindName(held->kind)) + ", and a tagged union's alternatives are fixed-layout");
                return false;
            }
            if (const TypeDescription* held = HeldTreeBacked(alternative.field.type))
            {
                Report(prefix + "its alternative " + Quoted(alternative.field.name) + " holds " + Named(*held) +
                       ", which is tree-backed, and a tagged union's alternatives are fixed-layout");
                return false;
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

    bool TypeRegistry::Unregister(std::string_view name)
    {
        const auto found = m_names.find(name);
        if (found == m_names.end())
        {
            Report("cannot unregister " + Quoted(name) + ": no type is registered under that name");
            return false;
        }
        const std::size_t index = found->second;
        const TypeDescription& description = *m_entries[index]->description;

        for (const auto& entry : m_entries)
        {
            const TypeDescription& other = *entry->description;
            if (&other != &description && !other.base.empty() && Find(other.base) == &description)
            {
                Report("cannot unregister " + Named(description) + ": " + Named(other) +
                       " extends it; unregister that type first");
                return false;
            }
            // A field holding the type points at its description, which its registrant frees once it is removed.
            for (const FieldInfo& field : other.fields)
            {
                if (&other != &description && Holds(field.type, description))
                {
                    Report("cannot unregister " + Named(description) + ": " + Named(other) + "'s field " +
                           Quoted(field.name) + " holds it; unregister that type first");
                    return false;
                }
            }
        }

        // The names point at entries by index, so those after the removed one move down by one.
        for (auto it = m_names.begin(); it != m_names.end();)
        {
            if (it->second == index)
            {
                it = m_names.erase(it);
                continue;
            }
            if (it->second > index)
                --it->second;
            ++it;
        }
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
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

    bool TypeRegistry::DetachSlot(std::string_view name, std::type_index tag) noexcept
    {
        const Entry* found = FindEntry(name);
        // FindEntry hands out const entries; this registry owns them, and is not const here.
        return found != nullptr && const_cast<Entry*>(found)->attachments.erase(tag) != 0;
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
