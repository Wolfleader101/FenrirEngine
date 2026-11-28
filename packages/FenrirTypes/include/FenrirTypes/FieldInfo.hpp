#pragma once

#include "FenrirTypes/Kind.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Fenrir
{
    struct TypeDescription;

    /**
     * @brief How a value reference reaches the elements of one `std::vector<T>` without naming `T`.
     *
     * A description holds one per list element type; `ListOperationsOf<T>` is the one for `std::vector<T>`.
     */
    struct ListOperations
    {
        /** @brief The element count of the list at `list`. */
        std::size_t (*size)(const void* list) = nullptr;
        /** @brief Resizes the list at `list` to `count` elements, new ones value-initialised. */
        void (*resize)(void* list, std::size_t count) = nullptr;
        /** @brief The address of element `index` of the list at `list`, which must be below its size. */
        void* (*element)(void* list, std::size_t index) = nullptr;
        /** @brief `sizeof(std::vector<T>)`, for a list held in a fixed array. */
        std::size_t listSize = 0;
    };

    /**
     * @brief How a value reference reaches the value of one `std::optional<T>` without naming `T`.
     *
     * A description holds one per optional's value type; `OptionalOperationsOf<T>` is the one for `std::optional<T>`.
     */
    struct OptionalOperations
    {
        /** @brief Whether the optional at `optional` holds a value. */
        bool (*hasValue)(const void* optional) = nullptr;
        /** @brief Makes the optional at `optional` hold a value-initialised `T`, replacing any it held; its address. */
        void* (*emplace)(void* optional) = nullptr;
        /** @brief Makes the optional at `optional` empty. */
        void (*reset)(void* optional) = nullptr;
        /** @brief The address of the value the optional at `optional` holds, which it must hold. */
        void* (*value)(void* optional) = nullptr;
        /** @brief `sizeof(std::optional<T>)`, for an optional held in a fixed array. */
        std::size_t optionalSize = 0;
    };

    /** @brief The list operations for `std::vector<T>`. */
    template <typename T>
    inline constexpr ListOperations ListOperationsOf{
        [](const void* list) -> std::size_t { return static_cast<const std::vector<T>*>(list)->size(); },
        [](void* list, std::size_t count) { static_cast<std::vector<T>*>(list)->resize(count); },
        [](void* list, std::size_t index) -> void* {
            // std::vector<bool> packs its bits, so has no element to point at; a list of bools is a list of u8.
            static_assert(!std::is_same_v<T, bool>, "std::vector<bool> has no addressable elements");
            return static_cast<std::vector<T>*>(list)->data() + index;
        },
        sizeof(std::vector<T>),
    };

    /** @brief The optional operations for `std::optional<T>`. */
    template <typename T>
    inline constexpr OptionalOperations OptionalOperationsOf{
        [](const void* optional) { return static_cast<const std::optional<T>*>(optional)->has_value(); },
        [](void* optional) -> void* { return &static_cast<std::optional<T>*>(optional)->emplace(); },
        [](void* optional) { static_cast<std::optional<T>*>(optional)->reset(); },
        [](void* optional) -> void* { return &**static_cast<std::optional<T>*>(optional); },
        sizeof(std::optional<T>),
    };

    /**
     * @brief The type of one value: its kind and what the kind needs besides.
     *
     * A field's value has one, and so does each element of an array or list and the value of an optional, through
     * `element`. Everything a value reference does with a value it learns from here.
     */
    struct ValueType
    {
        /** @brief The value's kind. */
        Kind kind = Kind::I32;
        /** @brief For `Struct` and `Enum`: the value's type description. */
        const TypeDescription* type = nullptr;
        /** @brief For `Array`, `List` and `Optional`: the type of each element, or of the optional's value. */
        const ValueType* element = nullptr;
        /** @brief For `Array`: the element count; for `FixedString`: `N` of the `char[N]`, its terminator included. */
        std::size_t count = 0;
        /** @brief For `Reference`: the canonical name of the type the reference's target has, or extends. */
        std::string_view target;
        /** @brief For `List`: how its elements are reached. */
        const ListOperations* list = nullptr;
        /** @brief For `Optional`: how its value is reached. */
        const OptionalOperations* optional = nullptr;
        /**
         * @brief For `Enum`: the value is a tagged union's tag, which a value reference reads but refuses to write;
         * only `TypeDescription::SelectAlternative` changes it, with the live alternative.
         */
        bool unionTag = false;
    };

    /** @brief The size in bytes a value of this type takes, as an element of a fixed array would. */
    [[nodiscard]] std::size_t SizeOf(const ValueType& type) noexcept;

    /** @brief The inclusive range an inspector offers for a number (`@range min max`). */
    struct FieldRange
    {
        double min = 0.0;
        double max = 0.0;
    };

    /** @brief The fixed editor-metadata vocabulary a field may carry, each from its doc command (ADR-0074). */
    struct FieldMetadata
    {
        /** @brief `@range min max`. */
        std::optional<FieldRange> range;
        /** @brief `@step s`: the increment an inspector steps a number by. */
        std::optional<double> step;
        /** @brief `@display text`: the name an inspector shows in place of the field's; empty when not given. */
        std::string_view display;
        /** @brief `@group text`: the group an inspector shows the field under; empty when not given. */
        std::string_view group;
        /** @brief `@units text`: the units an inspector shows beside the value; empty when not given. */
        std::string_view units;
        /** @brief `@readonly`: an inspector shows the value but does not edit it. */
        bool readonly = false;
        /** @brief `@hidden`: an inspector does not show the field. */
        bool hidden = false;
        /** @brief `@multiline`: an inspector edits the text over several lines. */
        bool multiline = false;
    };

    /** @brief One field of a described struct: static data, held once per described type. */
    struct FieldInfo
    {
        /** @brief The C++ member name, which files spell. */
        std::string_view name;
        /** @brief The field value's kind, and its element or target type. */
        ValueType type;
        /** @brief Bytes from the start of the struct, for a fixed-layout type; unused for an owning type. */
        std::size_t offset = 0;
        /** @brief The names the field had before (`@formerly oldName`), which files written then spell. */
        std::span<const std::string_view> formerly;
        /** @brief The field's doc comment, without its doc commands. */
        std::string_view doc;
        /** @brief What an inspector needs to show the field. */
        FieldMetadata metadata;
        /**
         * @brief `@transient`: read-back state kept in the type's layout that no file holds, so a writer leaves it
         * out and a reader leaves it at its default (ADR-0078). A data asset's or settings type's transient field is
         * not described at all.
         */
        bool transient = false;
    };

    /** @brief One enumerator of a described enum. */
    struct Enumerator
    {
        /** @brief The C++ enumerator name, which files spell. */
        std::string_view name;
        /** @brief Its value, widened to 64 bits. */
        std::int64_t value = 0;
        /** @brief Its doc comment. */
        std::string_view doc;
    };

    /**
     * @brief One alternative of a described tagged union: a member of its anonymous union, an entry of its API
     * description's `kinds`.
     */
    struct Alternative
    {
        /** @brief The tag value that makes it the live alternative, widened to 64 bits. */
        std::int64_t tag = 0;
        /**
         * @brief The member: its name, its value's kind, its offset (the union's, where every alternative starts) and
         * its doc. Fixed-layout kinds only.
         */
        FieldInfo field;
    };
} // namespace Fenrir
