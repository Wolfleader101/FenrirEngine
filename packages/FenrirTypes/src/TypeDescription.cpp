#include "FenrirTypes/TypeDescription.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <cstddef>
#include <cstring>
#include <new>
#include <span>

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

        // Value-initialises the fixed-layout value of type `type` at `at`: a struct through its description, an entity
        // as the null entity, an array element by element, anything else as zero bytes.
        void ValueInitialise(void* at, const ValueType& type)
        {
            switch (type.kind)
            {
            case Kind::Struct:
                if (type.type != nullptr)
                    type.type->Construct(at);
                break;
            case Kind::Entity:
                ::new (at) EntityValue{};
                break;
            case Kind::Array:
                if (type.element != nullptr)
                    for (std::size_t i = 0; i < type.count; ++i)
                        ValueInitialise(static_cast<std::byte*>(at) + i * SizeOf(*type.element), *type.element);
                break;
            default:
                std::memset(at, 0, SizeOf(type));
                break;
            }
        }

        // Ends the fixed-layout value of type `type` at `at`; only a struct, through its description, has anything to
        // end.
        void EndValue(void* at, const ValueType& type)
        {
            if (type.kind == Kind::Struct && type.type != nullptr)
                type.type->Destroy(at);
            else if (type.kind == Kind::Array && type.element != nullptr)
                for (std::size_t i = 0; i < type.count; ++i)
                    EndValue(static_cast<std::byte*>(at) + i * SizeOf(*type.element), *type.element);
        }

        // The alternative among `alternatives` that `tagValue` selects; null when none does.
        const Alternative* Selected(std::span<const Alternative> alternatives, std::int64_t tagValue) noexcept
        {
            for (const Alternative& alternative : alternatives)
                if (alternative.tag == tagValue)
                    return &alternative;
            return nullptr;
        }
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
        if (IsTaggedUnion())
        {
            visitor.Field(tag, ValueRef(bytes + tag.offset, tag.type));
            if (const Alternative* live = LiveAlternative(instance))
                visitor.Field(live->field, ValueRef(bytes + live->field.offset, live->field.type));
            return;
        }
        for (const FieldInfo& field : fields)
            visitor.Field(field, ValueRef(bytes + field.offset, field.type));
    }

    void TypeDescription::Visit(const void* instance, ConstFieldVisitor& visitor) const
    {
        // The visit function takes a mutable instance, but every value reaches the visitor read-only.
        ReadOnly readOnly(visitor);
        Visit(const_cast<void*>(instance), readOnly);
    }

    const Alternative* TypeDescription::LiveAlternative(const void* instance) const noexcept
    {
        if (!IsTaggedUnion())
            return nullptr;
        return Selected(alternatives,
                        ConstValueRef(static_cast<const std::byte*>(instance) + tag.offset, tag.type).EnumValue());
    }

    bool TypeDescription::SelectAlternative(void* instance, std::int64_t tagValue) const
    {
        const Alternative* next = Selected(alternatives, tagValue);
        if (next == nullptr)
            return false;

        auto* bytes = static_cast<std::byte*>(instance);
        if (const Alternative* live = LiveAlternative(instance))
            EndValue(bytes + live->field.offset, live->field.type);
        ValueInitialise(bytes + next->field.offset, next->field.type);

        // The tag's own type refuses writes, which are this door's alone.
        ValueType writable = tag.type;
        writable.unionTag = false;
        ValueRef(bytes + tag.offset, writable).SetEnumValue(tagValue);
        return true;
    }
} // namespace Fenrir
