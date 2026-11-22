#pragma once

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fenrir
{
    struct TypeDescription;
    class FieldVisitor;
    class ConstFieldVisitor;

    /** @brief The two fields of a `Reference` value, an asset handle: its slot and the slot's generation. */
    struct ReferenceValue
    {
        /** @brief The slot in the store's table of the target type. */
        std::uint32_t index = 0;
        /** @brief The slot's generation when the asset was made; zero only in an empty handle. */
        std::uint32_t generation = 0;

        friend constexpr bool operator==(const ReferenceValue&, const ReferenceValue&) = default;
    };

    /**
     * @brief The two fields of an `Entity` value, laid out as `Fenrir::Entity` is: its id and the entity list it
     * belongs to, which FenrirTypes never reads. The default is the null entity.
     */
    struct EntityValue
    {
        /** @brief The entity's id in its entity list; all bits set for the null entity, as `Entity::Null` is. */
        std::uint32_t id = 0xFFFFFFFFu;
        /** @brief The entity list it belongs to; null for the null entity. */
        void* list = nullptr;

        friend constexpr bool operator==(const EntityValue&, const EntityValue&) = default;
    };

    /** @brief The kind a scalar C++ type is: the integers, the floats, `bool` and `char16_t`. */
    template <typename T>
    [[nodiscard]] consteval Kind ScalarKindOf() noexcept
    {
        if constexpr (std::is_same_v<T, std::int8_t>)
            return Kind::I8;
        else if constexpr (std::is_same_v<T, std::int16_t>)
            return Kind::I16;
        else if constexpr (std::is_same_v<T, std::int32_t>)
            return Kind::I32;
        else if constexpr (std::is_same_v<T, std::int64_t>)
            return Kind::I64;
        else if constexpr (std::is_same_v<T, std::uint8_t>)
            return Kind::U8;
        else if constexpr (std::is_same_v<T, std::uint16_t>)
            return Kind::U16;
        else if constexpr (std::is_same_v<T, std::uint32_t>)
            return Kind::U32;
        else if constexpr (std::is_same_v<T, std::uint64_t>)
            return Kind::U64;
        else if constexpr (std::is_same_v<T, float>)
            return Kind::F32;
        else if constexpr (std::is_same_v<T, double>)
            return Kind::F64;
        else if constexpr (std::is_same_v<T, bool>)
            return Kind::Bool;
        else
        {
            static_assert(std::is_same_v<T, char16_t>, "not a scalar kind's C++ type");
            return Kind::Char16;
        }
    }

    /**
     * @brief A mutable reference to one described value: typed access per kind, without the C++ type in hand.
     *
     * What a field visitor is given for each field, and what an array's or list's element and an optional's value
     * are reached through. It is a reference: copying it refers to the same value, and writing through a `const`
     * one writes the value. A call meant for another kind reads as the empty value (zero, empty text, no elements)
     * and writes nothing.
     */
    class ValueRef
    {
      public:
        /** @brief A reference to the value at `value`, of type `type`; both must outlive it. */
        ValueRef(void* value, const ValueType& type) noexcept : m_value(value), m_type(&type)
        {
        }

        /** @brief The value's kind. */
        [[nodiscard]] Kind GetKind() const noexcept
        {
            return m_type->kind;
        }

        /** @brief The value's type: its kind and its element or target type. */
        [[nodiscard]] const ValueType& Type() const noexcept
        {
            return *m_type;
        }

        /** @brief For a scalar kind: the value as its C++ type, when `T` is that type (`float` for `f32`); else null.
         */
        template <typename T>
        [[nodiscard]] T* Get() const noexcept
        {
            return m_type->kind == ScalarKindOf<T>() ? static_cast<T*>(m_value) : nullptr;
        }

        /** @brief For an `Enum`: its value, read at its underlying kind's width. */
        [[nodiscard]] std::int64_t EnumValue() const noexcept;

        /**
         * @brief For an `Enum`: sets its value, cut to its underlying kind's width.
         *
         * A tagged union's tag is refused: writing it alone would leave the union's bytes the old alternative's, so
         * `TypeDescription::SelectAlternative` is the one door that changes it.
         *
         * @return Whether the value was written: false for another kind, and for a tagged union's tag.
         */
        bool SetEnumValue(std::int64_t value) const noexcept;

        /** @brief For a `String`, `FixedString` or `Path`: the value as UTF-8 text (a path in its generic form). */
        [[nodiscard]] std::string Text() const;

        /** @brief For a `String`, `FixedString` or `Path`: sets the text; a fixed string cuts it to fit, on a UTF-8
         * boundary. */
        void SetText(std::string_view text) const;

        /** @brief For an `Array` or `List`: its element count. */
        [[nodiscard]] std::size_t Size() const noexcept;

        /** @brief For a `List`: resizes it to `count` elements, new ones value-initialised. */
        void Resize(std::size_t count) const;

        /** @brief For an `Array` or `List`: element `index`, which must be below `Size()`. */
        [[nodiscard]] ValueRef Element(std::size_t index) const noexcept;

        /** @brief For an `Optional`: whether it holds a value. */
        [[nodiscard]] bool HasValue() const noexcept;

        /** @brief For an `Optional`: makes it hold a value-initialised value, replacing any it held, and returns it. */
        ValueRef Emplace() const;

        /** @brief For an `Optional`: makes it empty. */
        void Reset() const noexcept;

        /** @brief For an `Optional`: the value it holds, which it must hold. */
        [[nodiscard]] ValueRef Value() const noexcept;

        /** @brief For a `Reference`: its two fields. The target type's name is `Type().target`. */
        [[nodiscard]] ReferenceValue Reference() const noexcept;

        /** @brief For a `Reference`: sets its two fields. */
        void SetReference(ReferenceValue reference) const noexcept;

        /** @brief For an `Entity`: its two fields. */
        [[nodiscard]] EntityValue Entity() const noexcept;

        /** @brief For an `Entity`: sets its two fields. */
        void SetEntity(EntityValue entity) const noexcept;

        /** @brief For a `Struct` or `Enum`: its type description; else null. */
        [[nodiscard]] const TypeDescription* Description() const noexcept;

        /** @brief For a `Struct`: pushes each of its fields to `visitor` once, through its own description. */
        void Visit(FieldVisitor& visitor) const;

      private:
        void* m_value;
        const ValueType* m_type;

        friend class ConstValueRef;
    };

    /**
     * @brief A read-only reference to one described value: `ValueRef`'s reads, and elements and values read-only too.
     *
     * What a read-only field visitor is given for each field. A `ValueRef` converts to one.
     */
    class ConstValueRef
    {
      public:
        /** @brief A reference to the value at `value`, of type `type`; both must outlive it. */
        ConstValueRef(const void* value, const ValueType& type) noexcept
            // Only the reads below reach the value, so dropping const here writes nothing.
            : m_ref(const_cast<void*>(value), type)
        {
        }

        /** @brief The same value as `ref`, read-only. */
        ConstValueRef(ValueRef ref) noexcept : m_ref(ref)
        {
        }

        /** @brief The value's kind. */
        [[nodiscard]] Kind GetKind() const noexcept
        {
            return m_ref.GetKind();
        }

        /** @brief The value's type: its kind and its element or target type. */
        [[nodiscard]] const ValueType& Type() const noexcept
        {
            return m_ref.Type();
        }

        /** @brief For a scalar kind: the value as its C++ type, when `T` is that type; else null. */
        template <typename T>
        [[nodiscard]] const T* Get() const noexcept
        {
            return m_ref.Get<T>();
        }

        /** @brief For an `Enum`: its value, read at its underlying kind's width. */
        [[nodiscard]] std::int64_t EnumValue() const noexcept
        {
            return m_ref.EnumValue();
        }

        /** @brief For a `String`, `FixedString` or `Path`: the value as UTF-8 text. */
        [[nodiscard]] std::string Text() const
        {
            return m_ref.Text();
        }

        /** @brief For an `Array` or `List`: its element count. */
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_ref.Size();
        }

        /** @brief For an `Array` or `List`: element `index`, which must be below `Size()`. */
        [[nodiscard]] ConstValueRef Element(std::size_t index) const noexcept
        {
            return m_ref.Element(index);
        }

        /** @brief For an `Optional`: whether it holds a value. */
        [[nodiscard]] bool HasValue() const noexcept
        {
            return m_ref.HasValue();
        }

        /** @brief For an `Optional`: the value it holds, which it must hold. */
        [[nodiscard]] ConstValueRef Value() const noexcept
        {
            return m_ref.Value();
        }

        /** @brief For a `Reference`: its two fields. */
        [[nodiscard]] ReferenceValue Reference() const noexcept
        {
            return m_ref.Reference();
        }

        /** @brief For an `Entity`: its two fields. */
        [[nodiscard]] EntityValue Entity() const noexcept
        {
            return m_ref.Entity();
        }

        /** @brief For a `Struct` or `Enum`: its type description; else null. */
        [[nodiscard]] const TypeDescription* Description() const noexcept
        {
            return m_ref.Description();
        }

        /** @brief For a `Struct`: pushes each of its fields to `visitor` once, read-only. */
        void Visit(ConstFieldVisitor& visitor) const;

      private:
        ValueRef m_ref;
    };
} // namespace Fenrir
