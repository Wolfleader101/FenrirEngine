#include "FenrirSerialisation/Bridge.hpp"

#include "HandedField.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Fenrir::Serialisation
{
    namespace
    {
        // One field as the visitor pushed it.
        struct VisitedField
        {
            const FieldInfo* info;
            ConstValueRef value;
        };

        // Keeps every field a visit pushes, in the order pushed, so a value's fields and its default's can be walked
        // side by side.
        class FieldCollector final : public ConstFieldVisitor
        {
          public:
            explicit FieldCollector(std::vector<VisitedField>& fields) noexcept : m_fields(fields)
            {
            }

            void Field(const FieldInfo& field, ConstValueRef value) override
            {
                m_fields.push_back({&field, value});
            }

          private:
            std::vector<VisitedField>& m_fields;
        };

        // The fields of the struct `value` refers to, as its description's visitor pushes them: for a tagged union,
        // its tag and then its live alternative.
        std::vector<VisitedField> FieldsOf(ConstValueRef value)
        {
            std::vector<VisitedField> fields;
            FieldCollector collector(fields);
            value.Visit(collector);
            return fields;
        }

        // An instance of a described type that its `Construct` made, destroyed with this: the defaults a value's
        // fields are compared with.
        class Instance
        {
          public:
            explicit Instance(const TypeDescription& type)
                : m_type(type), m_bytes(::operator new(type.size, std::align_val_t{type.alignment}))
            {
                try
                {
                    type.Construct(m_bytes);
                }
                catch (...)
                {
                    ::operator delete(m_bytes, std::align_val_t{type.alignment});
                    throw;
                }
            }

            ~Instance()
            {
                m_type.Destroy(m_bytes);
                ::operator delete(m_bytes, std::align_val_t{m_type.alignment});
            }

            Instance(const Instance&) = delete;
            Instance& operator=(const Instance&) = delete;

            [[nodiscard]] void* Get() const noexcept
            {
                return m_bytes;
            }

          private:
            const TypeDescription& m_type;
            void* m_bytes;
        };

        // Integers compare by value and floats by their bits, so a -0 or a NaN off its default is still written.
        template <typename T>
        bool SameScalar(ConstValueRef a, ConstValueRef b) noexcept
        {
            if constexpr (std::is_floating_point_v<T>)
                return std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(*a.Get<T>()) ==
                       std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(*b.Get<T>());
            else
                return *a.Get<T>() == *b.Get<T>();
        }

        // Whether two values of the same type are equal, a struct field by field and a list element by element.
        bool Equal(ConstValueRef a, ConstValueRef b)
        {
            switch (a.GetKind())
            {
            case Kind::I8:
                return SameScalar<std::int8_t>(a, b);
            case Kind::I16:
                return SameScalar<std::int16_t>(a, b);
            case Kind::I32:
                return SameScalar<std::int32_t>(a, b);
            case Kind::I64:
                return SameScalar<std::int64_t>(a, b);
            case Kind::U8:
                return SameScalar<std::uint8_t>(a, b);
            case Kind::U16:
                return SameScalar<std::uint16_t>(a, b);
            case Kind::U32:
                return SameScalar<std::uint32_t>(a, b);
            case Kind::U64:
                return SameScalar<std::uint64_t>(a, b);
            case Kind::F32:
                return SameScalar<float>(a, b);
            case Kind::F64:
                return SameScalar<double>(a, b);
            case Kind::Bool:
                return SameScalar<bool>(a, b);
            case Kind::Char16:
                return SameScalar<char16_t>(a, b);
            case Kind::Enum:
                return a.EnumValue() == b.EnumValue();
            case Kind::String:
            case Kind::FixedString:
            case Kind::Path:
                return a.Text() == b.Text();
            case Kind::Array:
            case Kind::List:
                if (a.Size() != b.Size())
                    return false;
                for (std::size_t i = 0; i < a.Size(); ++i)
                    if (!Equal(a.Element(i), b.Element(i)))
                        return false;
                return true;
            case Kind::Optional:
                if (a.HasValue() != b.HasValue())
                    return false;
                return !a.HasValue() || Equal(a.Value(), b.Value());
            case Kind::Reference:
                return a.Reference() == b.Reference();
            case Kind::Entity:
                return a.Entity() == b.Entity();
            case Kind::Pointer:
                // Never written, so never different from its default.
                return true;
            case Kind::Struct: {
                // A tagged union pushes its tag first, so two unions of different alternatives differ there before
                // their alternatives are compared.
                const std::vector<VisitedField> fieldsA = FieldsOf(a);
                const std::vector<VisitedField> fieldsB = FieldsOf(b);
                if (fieldsA.size() != fieldsB.size())
                    return false;
                for (std::size_t i = 0; i < fieldsA.size(); ++i)
                    if (fieldsA[i].info != fieldsB[i].info || !Equal(fieldsA[i].value, fieldsB[i].value))
                        return false;
                return true;
            }
            }
            return false;
        }

        // What a write callback gave: its string, or `null` when it is empty.
        Value StringOrNull(std::string text)
        {
            return text.empty() ? Value(nullptr) : Value(std::move(text));
        }

        Value ToValue(const Resolvers& resolvers, const FieldInfo* field, ConstValueRef value,
                      const ConstValueRef* defaults);

        // Adds to `object` each of `fields` that differs from the same field of `defaults`, leaving out pointers; a
        // field with no partner in `defaults` is compared with nothing and written whole.
        void AddFields(const Resolvers& resolvers, Object& object, const std::vector<VisitedField>& fields,
                       const std::vector<VisitedField>& defaults)
        {
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                const VisitedField& field = fields[i];
                if (field.info->type.kind == Kind::Pointer)
                    continue;
                const VisitedField* partner =
                    i < defaults.size() && defaults[i].info == field.info ? &defaults[i] : nullptr;
                if (field.info->type.kind == Kind::Optional && !field.value.HasValue())
                {
                    // Left out, a field reads back as its default: one holding a value needs the null that reads as
                    // disengaged.
                    if (partner != nullptr && partner->value.HasValue())
                        object.Add(std::string(field.info->name), Value(nullptr));
                    continue;
                }
                if (partner != nullptr && Equal(field.value, partner->value))
                    continue;
                object.Add(std::string(field.info->name),
                           ToValue(resolvers, field.info, field.value, partner != nullptr ? &partner->value : nullptr));
            }
        }

        // The enumerator name of an enum value; its number when no enumerator has it.
        Value EnumToValue(ConstValueRef value)
        {
            const std::int64_t number = value.EnumValue();
            if (const TypeDescription* type = value.Description())
                for (const Enumerator& enumerator : type->enumerators)
                    if (enumerator.value == number)
                        return Value(enumerator.name);
            return Value(number);
        }

        // A tagged union: `kind`, then the live alternative's fields alone, each compared with the same field of the
        // alternative as the default instance holds it, or as selecting it in a fresh instance makes it.
        Value UnionToValue(const Resolvers& resolvers, ConstValueRef value, ConstValueRef defaults)
        {
            const TypeDescription& type = *value.Description();
            const std::vector<VisitedField> fields = FieldsOf(value);
            Object object;
            object.Add("kind", EnumToValue(fields.front().value));
            if (fields.size() < 2)
                return object;
            const VisitedField& live = fields[1];

            std::vector<VisitedField> defaultFields = FieldsOf(defaults);
            std::optional<Instance> selected;
            if (defaultFields.size() < 2 || defaultFields[1].info != live.info)
            {
                selected.emplace(type);
                type.SelectAlternative(selected->Get(), fields.front().value.EnumValue());
                defaultFields = FieldsOf(ConstValueRef(selected->Get(), value.Type()));
            }
            const VisitedField* liveDefault =
                defaultFields.size() >= 2 && defaultFields[1].info == live.info ? &defaultFields[1] : nullptr;

            const TypeDescription* alternative = live.value.Description();
            if (live.info->type.kind == Kind::Struct && alternative != nullptr &&
                alternative->role != Role::BoundType && !alternative->IsTaggedUnion())
            {
                // Spread into the union's own object, so a reader finds `kind` and the alternative's fields side by
                // side.
                AddFields(resolvers, object, FieldsOf(live.value),
                          liveDefault != nullptr ? FieldsOf(liveDefault->value) : std::vector<VisitedField>{});
            }
            else if (liveDefault == nullptr || !Equal(live.value, liveDefault->value))
            {
                // An alternative that is not a plain struct has no fields to spread, so it is a member by its name.
                object.Add(
                    std::string(live.info->name),
                    ToValue(resolvers, live.info, live.value, liveDefault != nullptr ? &liveDefault->value : nullptr));
            }
            return object;
        }

        // The tree form of `value`, whose fields at the values `defaults` holds are left out; with no defaults, a
        // struct's fields are compared with an instance its description constructs. `field` is the field holding
        // `value`, or holding the array, list or optional it is an element of; null for the value `ToTree` was given.
        Value ToValue(const Resolvers& resolvers, const FieldInfo* field, ConstValueRef value,
                      const ConstValueRef* defaults)
        {
            switch (value.GetKind())
            {
            case Kind::I8:
                return Value(*value.Get<std::int8_t>());
            case Kind::I16:
                return Value(*value.Get<std::int16_t>());
            case Kind::I32:
                return Value(*value.Get<std::int32_t>());
            case Kind::I64:
                return Value(*value.Get<std::int64_t>());
            case Kind::U8:
                return Value(*value.Get<std::uint8_t>());
            case Kind::U16:
                return Value(*value.Get<std::uint16_t>());
            case Kind::U32:
                return Value(*value.Get<std::uint32_t>());
            case Kind::U64:
                return Value(*value.Get<std::uint64_t>());
            case Kind::F32:
                return Value(*value.Get<float>());
            case Kind::F64:
                return Value(*value.Get<double>());
            case Kind::Bool:
                return Value(*value.Get<bool>());
            case Kind::Char16:
                return Value(static_cast<std::uint16_t>(*value.Get<char16_t>()));
            case Kind::Enum:
                return EnumToValue(value);
            case Kind::String:
            case Kind::FixedString:
            case Kind::Path:
                return Value(value.Text());
            case Kind::Array:
            case Kind::List: {
                // An element has no default of its own place, so a struct element is compared with a constructed one.
                Array array;
                for (std::size_t i = 0; i < value.Size(); ++i)
                    array.Add(ToValue(resolvers, field, value.Element(i), nullptr));
                return array;
            }
            case Kind::Optional: {
                if (!value.HasValue())
                    return Value(nullptr);
                const bool defaultHolds = defaults != nullptr && defaults->HasValue();
                const ConstValueRef held = defaultHolds ? defaults->Value() : value.Value();
                return ToValue(resolvers, field, value.Value(), defaultHolds ? &held : nullptr);
            }
            case Kind::Reference:
                if (!resolvers.writeReference || field == nullptr)
                    return Value(nullptr);
                return StringOrNull(HandedField(*field, value.Type(), [&](const FieldInfo& handed) {
                    return resolvers.writeReference(value.Reference(), handed);
                }));
            case Kind::Entity:
                if (!resolvers.writeEntity || field == nullptr)
                    return Value(nullptr);
                return StringOrNull(HandedField(*field, value.Type(), [&](const FieldInfo& handed) {
                    return resolvers.writeEntity(value.Entity(), handed);
                }));
            case Kind::Pointer:
                return Value(nullptr);
            case Kind::Struct: {
                const TypeDescription* type = value.Description();
                if (type == nullptr)
                    return Value(Object{});
                if (type->role == Role::BoundType)
                {
                    Array components;
                    for (const VisitedField& component : FieldsOf(value))
                        components.Add(ToValue(resolvers, component.info, component.value, nullptr));
                    return components;
                }
                std::optional<Instance> constructed;
                ConstValueRef structDefaults = defaults != nullptr ? *defaults : value;
                if (defaults == nullptr)
                {
                    constructed.emplace(*type);
                    structDefaults = ConstValueRef(constructed->Get(), value.Type());
                }
                if (type->IsTaggedUnion())
                    return UnionToValue(resolvers, value, structDefaults);
                Object object;
                AddFields(resolvers, object, FieldsOf(value), FieldsOf(structDefaults));
                return object;
            }
            }
            return Value(nullptr);
        }
    } // namespace

    Value ToTree(const void* value, const TypeDescription& type, [[maybe_unused]] const TypeRegistry& registry,
                 const Resolvers& resolvers, [[maybe_unused]] const Value* prior)
    {
        const ValueType valueType{.kind = Kind::Struct, .type = &type};
        return ToValue(resolvers, nullptr, ConstValueRef(value, valueType), nullptr);
    }
} // namespace Fenrir::Serialisation
