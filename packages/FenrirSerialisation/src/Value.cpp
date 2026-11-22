#include "FenrirSerialisation/Value.hpp"

#include <cassert>
#include <cmath>
#include <type_traits>
#include <utility>

namespace Fenrir::Serialisation
{
    NumberWidth Number::Width() const noexcept
    {
        return std::visit(
            []<typename T>(const T&) {
                if constexpr (std::is_same_v<T, std::int64_t>)
                    return NumberWidth::Signed;
                else if constexpr (std::is_same_v<T, std::uint64_t>)
                    return NumberWidth::Unsigned;
                else if constexpr (std::is_same_v<T, float>)
                    return NumberWidth::Single;
                else
                    return NumberWidth::Double;
            },
            m_value);
    }

    std::int64_t Number::AsSigned() const
    {
        assert(Width() == NumberWidth::Signed && "the number is a signed integer");
        return std::get<std::int64_t>(m_value);
    }

    std::uint64_t Number::AsUnsigned() const
    {
        assert(Width() == NumberWidth::Unsigned && "the number is an unsigned integer");
        return std::get<std::uint64_t>(m_value);
    }

    float Number::AsSingle() const
    {
        assert(Width() == NumberWidth::Single && "the number is a single float");
        return std::get<float>(m_value);
    }

    double Number::AsDouble() const
    {
        assert(Width() == NumberWidth::Double && "the number is a double float");
        return std::get<double>(m_value);
    }

    bool operator==(const Number& a, const Number& b) noexcept
    {
        if (a.m_value.index() != b.m_value.index())
            return false;
        return std::visit(
            [&b]<typename T>(const T& value) {
                const T& other = std::get<T>(b.m_value);
                if constexpr (std::is_floating_point_v<T>)
                {
                    // Two NaNs are equal here, so a tree read from text holding `NaN` equals itself.
                    if (std::isnan(value) || std::isnan(other))
                        return std::isnan(value) && std::isnan(other);
                }
                return value == other;
            },
            a.m_value);
    }

    Value* Object::Find(std::string_view key) noexcept
    {
        for (Member& member : members)
        {
            if (member.key == key)
                return &member.value;
        }
        return nullptr;
    }

    const Value* Object::Find(std::string_view key) const noexcept
    {
        for (const Member& member : members)
        {
            if (member.key == key)
                return &member.value;
        }
        return nullptr;
    }

    Member& Object::Add(std::string key, Value value)
    {
        return members.emplace_back(Member{std::move(key), std::move(value), {}, std::nullopt});
    }

    Element& Array::Add(Value value)
    {
        return elements.emplace_back(Element{std::move(value), {}, std::nullopt});
    }

    Value::Value(Object object) : m_value(std::move(object))
    {
    }

    Value::Value(Array array) : m_value(std::move(array))
    {
    }

    Value::Value(std::string text) : m_value(std::move(text))
    {
    }

    Value::Value(std::string_view text) : m_value(std::string(text))
    {
    }

    Value::Value(const char* text) : m_value(std::string(text))
    {
    }

    Value::Value(Number number) : m_value(number)
    {
    }

    ValueKind Value::Kind() const noexcept
    {
        return std::visit(
            []<typename T>(const T&) {
                if constexpr (std::is_same_v<T, Object>)
                    return ValueKind::Object;
                else if constexpr (std::is_same_v<T, Array>)
                    return ValueKind::Array;
                else if constexpr (std::is_same_v<T, std::string>)
                    return ValueKind::String;
                else if constexpr (std::is_same_v<T, Number>)
                    return ValueKind::Number;
                else if constexpr (std::is_same_v<T, bool>)
                    return ValueKind::Bool;
                else
                    return ValueKind::Null;
            },
            m_value);
    }

    Object& Value::AsObject()
    {
        assert(Kind() == ValueKind::Object && "the value is an object");
        return std::get<Object>(m_value);
    }

    const Object& Value::AsObject() const
    {
        assert(Kind() == ValueKind::Object && "the value is an object");
        return std::get<Object>(m_value);
    }

    Array& Value::AsArray()
    {
        assert(Kind() == ValueKind::Array && "the value is an array");
        return std::get<Array>(m_value);
    }

    const Array& Value::AsArray() const
    {
        assert(Kind() == ValueKind::Array && "the value is an array");
        return std::get<Array>(m_value);
    }

    const std::string& Value::AsString() const
    {
        assert(Kind() == ValueKind::String && "the value is a string");
        return std::get<std::string>(m_value);
    }

    const Number& Value::AsNumber() const
    {
        assert(Kind() == ValueKind::Number && "the value is a number");
        return std::get<Number>(m_value);
    }

    bool Value::AsBool() const
    {
        assert(Kind() == ValueKind::Bool && "the value is a bool");
        return std::get<bool>(m_value);
    }

    namespace
    {
        bool EqualNodes(const Value& a, const Value& b, Trivia trivia);

        bool EqualComments(const auto& a, const auto& b, Trivia trivia)
        {
            return trivia == Trivia::Drop || a == b;
        }

        bool EqualNodes(const Value& a, const Value& b, Trivia trivia)
        {
            if (a.Kind() != b.Kind())
                return false;
            switch (a.Kind())
            {
            case ValueKind::Object: {
                const Object& x = a.AsObject();
                const Object& y = b.AsObject();
                if (x.members.size() != y.members.size() ||
                    !EqualComments(x.closingComments, y.closingComments, trivia))
                    return false;
                for (std::size_t i = 0; i < x.members.size(); ++i)
                {
                    const Member& m = x.members[i];
                    const Member& n = y.members[i];
                    if (m.key != n.key || !EqualComments(m.leadingComments, n.leadingComments, trivia) ||
                        !EqualComments(m.trailingComment, n.trailingComment, trivia) ||
                        !EqualNodes(m.value, n.value, trivia))
                        return false;
                }
                return true;
            }
            case ValueKind::Array: {
                const Array& x = a.AsArray();
                const Array& y = b.AsArray();
                if (x.elements.size() != y.elements.size() ||
                    !EqualComments(x.closingComments, y.closingComments, trivia))
                    return false;
                for (std::size_t i = 0; i < x.elements.size(); ++i)
                {
                    const Element& e = x.elements[i];
                    const Element& f = y.elements[i];
                    if (!EqualComments(e.leadingComments, f.leadingComments, trivia) ||
                        !EqualComments(e.trailingComment, f.trailingComment, trivia) ||
                        !EqualNodes(e.value, f.value, trivia))
                        return false;
                }
                return true;
            }
            case ValueKind::String:
                return a.AsString() == b.AsString();
            case ValueKind::Number:
                return a.AsNumber() == b.AsNumber();
            case ValueKind::Bool:
                return a.AsBool() == b.AsBool();
            case ValueKind::Null:
                return true;
            }
            return false;
        }
    } // namespace

    bool operator==(const Value& a, const Value& b)
    {
        return EqualNodes(a, b, Trivia::Drop);
    }

    bool Equals(const Value& a, const Value& b, Trivia trivia)
    {
        return EqualComments(a.leadingComments, b.leadingComments, trivia) && EqualNodes(a, b, trivia);
    }
} // namespace Fenrir::Serialisation
