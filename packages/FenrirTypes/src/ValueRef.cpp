#include "FenrirTypes/ValueRef.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <string>

namespace Fenrir
{
    namespace
    {
        static_assert(sizeof(EntityValue) == 16, "an entity is 16 bytes: its id, then its entity list pointer");

        std::size_t ScalarSize(Kind kind) noexcept
        {
            switch (kind)
            {
            case Kind::I8:
            case Kind::U8:
            case Kind::Bool:
                return 1;
            case Kind::I16:
            case Kind::U16:
            case Kind::Char16:
                return 2;
            case Kind::I32:
            case Kind::U32:
            case Kind::F32:
                return 4;
            case Kind::I64:
            case Kind::U64:
            case Kind::F64:
                return 8;
            default:
                return 0;
            }
        }

        template <typename T>
        T Load(const void* at) noexcept
        {
            T value;
            std::memcpy(&value, at, sizeof(T));
            return value;
        }

        template <typename T>
        void Store(void* at, T value) noexcept
        {
            std::memcpy(at, &value, sizeof(T));
        }

        // How many of the first `limit` bytes of `text` to keep so a UTF-8 sequence is never split.
        std::size_t Utf8Cut(std::string_view text, std::size_t limit) noexcept
        {
            if (text.size() <= limit)
                return text.size();
            std::size_t cut = limit;
            // Back off over continuation bytes (10xxxxxx): the byte at `cut` would start the part left out.
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u)
                --cut;
            return cut;
        }
    } // namespace

    std::size_t SizeOf(const ValueType& type) noexcept
    {
        switch (type.kind)
        {
        case Kind::Enum:
        case Kind::Struct:
            return type.type != nullptr ? type.type->size : 0;
        case Kind::Array:
            return type.element != nullptr ? type.count * SizeOf(*type.element) : 0;
        case Kind::Pointer:
            return sizeof(void*);
        case Kind::String:
            return sizeof(std::string);
        case Kind::List:
            return type.list != nullptr ? type.list->listSize : 0;
        case Kind::Optional:
            return type.optional != nullptr ? type.optional->optionalSize : 0;
        case Kind::Reference:
            return sizeof(ReferenceValue);
        case Kind::Entity:
            return sizeof(EntityValue);
        case Kind::FixedString:
            return type.count;
        case Kind::Path:
            return sizeof(std::filesystem::path);
        default:
            return ScalarSize(type.kind);
        }
    }

    std::int64_t ValueRef::EnumValue() const noexcept
    {
        if (m_type->kind != Kind::Enum || m_type->type == nullptr)
            return 0;
        switch (m_type->type->underlying)
        {
        case Kind::I8:
            return Load<std::int8_t>(m_value);
        case Kind::I16:
            return Load<std::int16_t>(m_value);
        case Kind::I32:
            return Load<std::int32_t>(m_value);
        case Kind::I64:
            return Load<std::int64_t>(m_value);
        case Kind::U8:
            return Load<std::uint8_t>(m_value);
        case Kind::U16:
            return Load<std::uint16_t>(m_value);
        case Kind::U32:
            return Load<std::uint32_t>(m_value);
        case Kind::U64:
            return static_cast<std::int64_t>(Load<std::uint64_t>(m_value));
        default:
            return 0;
        }
    }

    bool ValueRef::SetEnumValue(std::int64_t value) const noexcept
    {
        if (m_type->kind != Kind::Enum || m_type->type == nullptr || m_type->unionTag)
            return false;
        switch (m_type->type->underlying)
        {
        case Kind::I8:
            Store(m_value, static_cast<std::int8_t>(value));
            return true;
        case Kind::I16:
            Store(m_value, static_cast<std::int16_t>(value));
            return true;
        case Kind::I32:
            Store(m_value, static_cast<std::int32_t>(value));
            return true;
        case Kind::I64:
            Store(m_value, value);
            return true;
        case Kind::U8:
            Store(m_value, static_cast<std::uint8_t>(value));
            return true;
        case Kind::U16:
            Store(m_value, static_cast<std::uint16_t>(value));
            return true;
        case Kind::U32:
            Store(m_value, static_cast<std::uint32_t>(value));
            return true;
        case Kind::U64:
            Store(m_value, static_cast<std::uint64_t>(value));
            return true;
        default:
            return false;
        }
    }

    std::string ValueRef::Text() const
    {
        switch (m_type->kind)
        {
        case Kind::String:
            return *static_cast<const std::string*>(m_value);
        case Kind::FixedString: {
            const auto* chars = static_cast<const char*>(m_value);
            std::size_t length = 0;
            while (length < m_type->count && chars[length] != '\0')
                ++length;
            return std::string(chars, length);
        }
        case Kind::Path: {
            const std::u8string text = static_cast<const std::filesystem::path*>(m_value)->generic_u8string();
            return std::string(reinterpret_cast<const char*>(text.data()), text.size());
        }
        default:
            return {};
        }
    }

    void ValueRef::SetText(std::string_view text) const
    {
        switch (m_type->kind)
        {
        case Kind::String:
            static_cast<std::string*>(m_value)->assign(text);
            break;
        case Kind::FixedString: {
            if (m_type->count == 0)
                break;
            const std::size_t kept = Utf8Cut(text, m_type->count - 1);
            auto* chars = static_cast<char*>(m_value);
            std::memcpy(chars, text.data(), kept);
            std::memset(chars + kept, 0, m_type->count - kept);
            break;
        }
        case Kind::Path: {
            const std::u8string utf8(reinterpret_cast<const char8_t*>(text.data()), text.size());
            *static_cast<std::filesystem::path*>(m_value) = std::filesystem::path(utf8);
            break;
        }
        default:
            break;
        }
    }

    std::size_t ValueRef::Size() const noexcept
    {
        if (m_type->kind == Kind::Array)
            return m_type->count;
        if (m_type->kind == Kind::List && m_type->list != nullptr)
            return m_type->list->size(m_value);
        return 0;
    }

    void ValueRef::Resize(std::size_t count) const
    {
        if (m_type->kind == Kind::List && m_type->list != nullptr)
            m_type->list->resize(m_value, count);
    }

    ValueRef ValueRef::Element(std::size_t index) const noexcept
    {
        assert(index < Size() && "element index out of range");
        assert(m_type->element != nullptr && "an array or list names its element type");
        if (m_type->kind == Kind::List)
            return ValueRef(m_type->list->element(m_value, index), *m_type->element);
        return ValueRef(static_cast<std::byte*>(m_value) + index * SizeOf(*m_type->element), *m_type->element);
    }

    bool ValueRef::HasValue() const noexcept
    {
        return m_type->kind == Kind::Optional && m_type->optional != nullptr && m_type->optional->hasValue(m_value);
    }

    ValueRef ValueRef::Emplace() const
    {
        assert(m_type->kind == Kind::Optional && m_type->optional != nullptr && m_type->element != nullptr);
        return ValueRef(m_type->optional->emplace(m_value), *m_type->element);
    }

    void ValueRef::Reset() const noexcept
    {
        if (m_type->kind == Kind::Optional && m_type->optional != nullptr)
            m_type->optional->reset(m_value);
    }

    ValueRef ValueRef::Value() const noexcept
    {
        assert(HasValue() && "the optional holds a value");
        return ValueRef(m_type->optional->value(m_value), *m_type->element);
    }

    ReferenceValue ValueRef::Reference() const noexcept
    {
        return m_type->kind == Kind::Reference ? Load<ReferenceValue>(m_value) : ReferenceValue{};
    }

    void ValueRef::SetReference(ReferenceValue reference) const noexcept
    {
        if (m_type->kind == Kind::Reference)
            Store(m_value, reference);
    }

    EntityValue ValueRef::Entity() const noexcept
    {
        return m_type->kind == Kind::Entity ? Load<EntityValue>(m_value) : EntityValue{};
    }

    void ValueRef::SetEntity(EntityValue entity) const noexcept
    {
        if (m_type->kind == Kind::Entity)
            Store(m_value, entity);
    }

    const TypeDescription* ValueRef::Description() const noexcept
    {
        return m_type->kind == Kind::Struct || m_type->kind == Kind::Enum ? m_type->type : nullptr;
    }

    void ValueRef::Visit(FieldVisitor& visitor) const
    {
        if (m_type->kind == Kind::Struct && m_type->type != nullptr)
            m_type->type->Visit(m_value, visitor);
    }

    bool ValueRef::SelectAlternative(std::int64_t tagValue) const
    {
        if (m_type->kind != Kind::Struct || m_type->type == nullptr)
            return false;
        return m_type->type->SelectAlternative(m_value, tagValue);
    }

    void ConstValueRef::Visit(ConstFieldVisitor& visitor) const
    {
        if (m_ref.m_type->kind == Kind::Struct && m_ref.m_type->type != nullptr)
            m_ref.m_type->type->Visit(static_cast<const void*>(m_ref.m_value), visitor);
    }
} // namespace Fenrir
