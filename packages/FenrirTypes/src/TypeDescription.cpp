#include "FenrirTypes/TypeDescription.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <cassert>
#include <cstddef>

namespace Fenrir
{
    void TypeDescription::Construct(void* instance) const
    {
        if (construct != nullptr)
            construct(instance);
    }

    void TypeDescription::Destroy(void* instance) const
    {
        if (destroy != nullptr)
            destroy(instance);
    }

    void TypeDescription::Visit(void* instance, FieldVisitor& visitor) const
    {
        // The table-driven form: each field at its offset. An owning type's fields have no offsets, so it is walked
        // by a generated function instead, never through here.
        assert(!AdmitsOwningKinds(role) && "an owning type is not visited through offsets");
        auto* bytes = static_cast<std::byte*>(instance);
        for (const FieldInfo& field : fields)
            visitor.Field(field, ValueRef(bytes + field.offset, field.type));
    }

    void TypeDescription::Visit(const void* instance, ConstFieldVisitor& visitor) const
    {
        assert(!AdmitsOwningKinds(role) && "an owning type is not visited through offsets");
        const auto* bytes = static_cast<const std::byte*>(instance);
        for (const FieldInfo& field : fields)
            visitor.Field(field, ConstValueRef(bytes + field.offset, field.type));
    }
} // namespace Fenrir
