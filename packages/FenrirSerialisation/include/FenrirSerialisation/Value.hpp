#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Fenrir::Serialisation
{
    class Value;
    struct Member;
    struct Element;

    /** @brief What a value tree node is: the six JSON5 values (ADR-0079 §1). */
    enum class ValueKind
    {
        Object,
        Array,
        String,
        Number,
        Bool,
        Null,
    };

    /** @brief Whether a comparison of two value trees looks at their comments as well as their values. */
    enum class Trivia
    {
        Ignore,
        Compare,
    };

    /** @brief Where a node began in the text it was read from: both 1-based. */
    struct Location
    {
        /** @brief The line, counted from 1. */
        std::uint32_t line = 1;
        /** @brief The column on that line, counted from 1. */
        std::uint32_t column = 1;

        friend bool operator==(const Location&, const Location&) = default;
    };

    /** @brief The width a `Number` holds its value in, which is the form the writer prints it in. */
    enum class NumberWidth
    {
        Signed,
        Unsigned,
        Single,
        Double,
    };

    /**
     * @brief A number in its own width: a signed or unsigned 64-bit integer, a single or a double float. A 64-bit
     * integer never passes through a `double`, and a single float prints in its own shortest form (`0.7071068`).
     */
    class Number
    {
      public:
        /** @brief Zero, a signed integer. */
        Number() = default;

        /** @brief A signed integer. */
        template <std::signed_integral T>
            requires(!std::same_as<T, bool>)
        Number(T value) : m_value(static_cast<std::int64_t>(value))
        {
        }

        /** @brief An unsigned integer. */
        template <std::unsigned_integral T>
            requires(!std::same_as<T, bool>)
        Number(T value) : m_value(static_cast<std::uint64_t>(value))
        {
        }

        /** @brief A single-precision float. */
        Number(float value) : m_value(value)
        {
        }

        /** @brief A double-precision float. */
        Number(double value) : m_value(value)
        {
        }

        /** @brief The width the value is held in. */
        [[nodiscard]] NumberWidth Width() const noexcept;

        /** @brief For a `Signed` number: its value. */
        [[nodiscard]] std::int64_t AsSigned() const;
        /** @brief For an `Unsigned` number: its value. */
        [[nodiscard]] std::uint64_t AsUnsigned() const;
        /** @brief For a `Single` number: its value. */
        [[nodiscard]] float AsSingle() const;
        /** @brief For a `Double` number: its value. */
        [[nodiscard]] double AsDouble() const;

        /**
         * @brief Equal when the width and the value both are; a NaN equals a NaN of its width, so a tree equals
         * itself.
         */
        friend bool operator==(const Number& a, const Number& b) noexcept;

      private:
        std::variant<std::int64_t, std::uint64_t, float, double> m_value{std::int64_t{0}};
    };

    /** @brief An object: members in the order read or added, and the comments before its closing brace. */
    struct Object
    {
        /** @brief The members, in the order read or added, which is the order written. */
        std::vector<Member> members;
        /** @brief Comments between the last member and the closing brace, verbatim with their delimiters. */
        std::vector<std::string> closingComments;

        /** @brief The value of the first member named `key`, by a linear search; null when there is none. */
        [[nodiscard]] Value* Find(std::string_view key) noexcept;
        /** @brief The value of the first member named `key`, by a linear search; null when there is none. */
        [[nodiscard]] const Value* Find(std::string_view key) const noexcept;

        /** @brief Appends a member named `key` holding `value`, and returns it so comments can be set on it. */
        Member& Add(std::string key, Value value);
    };

    /** @brief An array: elements in order, and the comments before its closing bracket. */
    struct Array
    {
        /** @brief The elements, in order. */
        std::vector<Element> elements;
        /** @brief Comments between the last element and the closing bracket, verbatim with their delimiters. */
        std::vector<std::string> closingComments;

        /** @brief Appends an element holding `value`, and returns it so comments can be set on it. */
        Element& Add(Value value);
    };

    /**
     * @brief One node of a value tree (glossary "Value tree"): an object, array, string, number, bool or null, with
     * the location it was read from. Comments live on the members and elements that hold nodes, and on the root's
     * leading comments.
     */
    class Value
    {
      public:
        /** @brief A null. */
        Value() = default;
        /** @brief A null. */
        Value(std::nullptr_t) noexcept
        {
        }
        /** @brief An object. */
        Value(Object object);
        /** @brief An array. */
        Value(Array array);
        /** @brief A string, UTF-8. */
        Value(std::string text);
        /** @brief A string, UTF-8. */
        Value(std::string_view text);
        /** @brief A string, UTF-8. */
        Value(const char* text);
        /** @brief A number. */
        Value(Number number);
        /** @brief A bool. Only `bool` itself: a pointer or an integer is not turned into one. */
        template <std::same_as<bool> T>
        Value(T flag) : m_value(flag)
        {
        }
        /** @brief A number from an integer or a float, in that type's width. */
        template <typename T>
            requires((std::integral<T> && !std::same_as<T, bool>) || std::floating_point<T>)
        Value(T number) : m_value(Number(number))
        {
        }

        /** @brief What the node is. */
        [[nodiscard]] ValueKind Kind() const noexcept;

        /** @brief For an `Object`: the object. */
        [[nodiscard]] Object& AsObject();
        /** @brief For an `Object`: the object. */
        [[nodiscard]] const Object& AsObject() const;
        /** @brief For an `Array`: the array. */
        [[nodiscard]] Array& AsArray();
        /** @brief For an `Array`: the array. */
        [[nodiscard]] const Array& AsArray() const;
        /** @brief For a `String`: its UTF-8 text. */
        [[nodiscard]] const std::string& AsString() const;
        /** @brief For a `Number`: the number. */
        [[nodiscard]] const Number& AsNumber() const;
        /** @brief For a `Bool`: its value. */
        [[nodiscard]] bool AsBool() const;

        /** @brief For a node read from text, where it began; empty for a node built in code. */
        std::optional<Location> location;
        /** @brief For the root of a tree: the comment lines above it, verbatim with their delimiters. */
        std::vector<std::string> leadingComments;

        /** @brief Equal values, ignoring locations and comments. */
        friend bool operator==(const Value& a, const Value& b);

      private:
        std::variant<std::nullptr_t, Object, Array, std::string, Number, bool> m_value;
    };

    /** @brief One member of an object: its key, its value and the comments around it. */
    struct Member
    {
        /** @brief The key, UTF-8. */
        std::string key;
        /** @brief The value. */
        Value value;
        /** @brief The comment lines above the member, verbatim with their delimiters. */
        std::vector<std::string> leadingComments;
        /** @brief A comment after the member on its own line, verbatim with its delimiters. */
        std::optional<std::string> trailingComment;
    };

    /** @brief One element of an array: its value and the comments around it. */
    struct Element
    {
        /** @brief The value. */
        Value value;
        /** @brief The comment lines above the element, verbatim with their delimiters. */
        std::vector<std::string> leadingComments;
        /** @brief A comment after the element on its own line, verbatim with its delimiters. */
        std::optional<std::string> trailingComment;
    };

    /**
     * @brief Whether two trees hold equal values, ignoring locations; with `Trivia::Compare` their comments must be
     * equal too, each in the same place.
     */
    [[nodiscard]] bool Equals(const Value& a, const Value& b, Trivia trivia = Trivia::Ignore);
} // namespace Fenrir::Serialisation
