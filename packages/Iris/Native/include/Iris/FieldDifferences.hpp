#pragma once

#include "Method.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Iris
{
    /**
     * @brief One kind of a tagged union: the tag value that selects it, as the tag's bytes read as an unsigned
     * integer, and the index in the mapping's fields of the field it selects.
     */
    struct UnionKind
    {
        std::uint64_t tag;
        std::size_t field;
    };

    /**
     * @brief The layout of a tagged union: the mapped type's name (its StructMapping's typeName) and its kinds. The
     * tag is the mapping's first field. Which field a tag value selects is the binding's knowledge, not the
     * mapping's, so a caller of FieldDifferences names it here.
     */
    struct UnionLayout
    {
        std::string_view typeName;
        std::vector<UnionKind> kinds;
    };

    namespace Detail
    {
        constexpr std::size_t ScalarSize(ValueKind kind) noexcept
        {
            switch (kind)
            {
            case ValueKind::Bool:
            case ValueKind::Int8:
            case ValueKind::UInt8:
                return 1;
            case ValueKind::Int16:
            case ValueKind::UInt16:
            case ValueKind::Char:
                return 2;
            case ValueKind::Int32:
            case ValueKind::UInt32:
            case ValueKind::Float:
                return 4;
            default:
                return 8;
            }
        }

        inline std::string Hex(std::span<const std::byte> bytes)
        {
            constexpr std::string_view digits = "0123456789abcdef";
            std::string text;
            for (const std::byte byte : bytes)
            {
                text += digits[static_cast<unsigned>(byte) >> 4];
                text += digits[static_cast<unsigned>(byte) & 15];
            }
            return text;
        }

        // The walk of one mapping over two values' bytes: each field at its offset, nested structs and array elements
        // recursively, never padding, and of a tagged union only the tag and the kind the left value's tag selects.
        struct FieldComparison
        {
            std::span<const std::byte> left;
            std::span<const std::byte> right;
            std::span<const UnionLayout> unions;
            std::vector<std::string> differences;

            void Bytes(std::size_t offset, std::size_t size, const std::string& path)
            {
                const auto leftBytes = left.subspan(offset, size);
                const auto rightBytes = right.subspan(offset, size);
                if (std::memcmp(leftBytes.data(), rightBytes.data(), size) != 0)
                {
                    differences.push_back(path + " at offset " + std::to_string(offset) + ": left " + Hex(leftBytes) +
                                          ", right " + Hex(rightBytes));
                }
            }

            std::uint64_t Unsigned(std::size_t offset, std::size_t size) const
            {
                std::uint64_t value = 0;
                std::memcpy(&value, left.subspan(offset, size).data(), size);
                return value;
            }

            void Value(ValueKind kind, const StructInfo* nested, std::size_t offset, const std::string& path)
            {
                if (kind == ValueKind::Struct)
                    Struct(*nested, offset, path);
                else
                    Bytes(offset, kind == ValueKind::Enum ? nested->size : ScalarSize(kind), path);
            }

            void Struct(const StructInfo& info, std::size_t offset, const std::string& path)
            {
                for (const UnionLayout& union_ : unions)
                {
                    if (union_.typeName != info.typeName)
                        continue;
                    const auto& tag = info.fields[0];
                    Value(tag.kind, tag.nested, offset + tag.offset, path + "." + std::string(tag.name));
                    const std::uint64_t value = Unsigned(offset + tag.offset, tag.nested->size);
                    for (const UnionKind& kind : union_.kinds)
                    {
                        if (kind.tag == value)
                        {
                            const auto& field = info.fields[kind.field];
                            Value(field.kind, field.nested, offset + field.offset,
                                  path + "." + std::string(field.name));
                        }
                    }
                    return;
                }
                for (const auto& field : info.fields)
                {
                    const std::string name = path + "." + std::string(field.name);
                    if (field.kind != ValueKind::InlineArray)
                    {
                        Value(field.kind, field.nested, offset + field.offset, name);
                        continue;
                    }
                    const std::size_t size = field.element == ValueKind::Struct || field.element == ValueKind::Enum
                                                 ? field.nested->size
                                                 : ScalarSize(field.element);
                    for (std::size_t index = 0; index < field.length; ++index)
                    {
                        Value(field.element, field.nested, offset + field.offset + index * size,
                              name + "[" + std::to_string(index) + "]");
                    }
                }
            }
        };
    } // namespace Detail

    /**
     * @brief The fields of two values of a mapped struct that differ, compared through T's StructMapping: the bytes
     * of each field at its offset, recursing into nested structs and fixed-size array elements, never padding. Each
     * difference reads `<path> at offset <n>: left <hex>, right <hex>`, the path starting at the mapping's type name
     * (`My.Pose.Position.X at offset 0: left 0000803f, right 00000000`). Of a tagged union named in `unions` only
     * the tag and the kind the left value's tag selects are compared: the other kinds' bytes are not set. Equal values
     * give an empty vector.
     */
    template <StructValue T>
    [[nodiscard]] std::vector<std::string> FieldDifferences(const T& left, const T& right,
                                                            std::span<const UnionLayout> unions = {})
    {
        const Detail::StructInfo& info = Detail::StructInfoOf<T>;
        Detail::FieldComparison comparison{
            std::as_bytes(std::span(&left, 1)), std::as_bytes(std::span(&right, 1)), unions, {}};
        comparison.Struct(info, 0, std::string(info.typeName));
        return std::move(comparison.differences);
    }
} // namespace Iris
