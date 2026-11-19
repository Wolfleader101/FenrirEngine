#include "FenrirTypes/TypeDescription.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <cstddef>

namespace Fenrir
{
    namespace
    {
        // Hands a read-only visitor what a type's visit function pushes, read-only, so one generated function serves
        // both forms of Visit.
        class ReadOnly final : public FieldVisitor
        {
          public:
            explicit ReadOnly(ConstFieldVisitor& visitor) noexcept : m_visitor(visitor)
            {
            }

            void Field(const FieldInfo& field, ValueRef value) override
            {
                m_visitor.Field(field, value);
            }

          private:
            ConstFieldVisitor& m_visitor;
        };
    } // namespace

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
        if (visit != nullptr)
        {
            visit(instance, visitor);
            return;
        }
        auto* bytes = static_cast<std::byte*>(instance);
        for (const FieldInfo& field : fields)
            visitor.Field(field, ValueRef(bytes + field.offset, field.type));
    }

    void TypeDescription::Visit(const void* instance, ConstFieldVisitor& visitor) const
    {
        // The visit function takes a mutable instance, but every value reaches the visitor read-only.
        ReadOnly readOnly(visitor);
        Visit(const_cast<void*>(instance), readOnly);
    }
} // namespace Fenrir
