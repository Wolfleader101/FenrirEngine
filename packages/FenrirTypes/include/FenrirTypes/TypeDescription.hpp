#pragma once

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>

namespace Fenrir
{
    class FieldVisitor;
    class ConstFieldVisitor;

    /** @brief Value-initialises a `T` at `instance`: the `construct` a description of `T` holds. */
    template <typename T>
    void ConstructValue(void* instance)
    {
        ::new (instance) T{};
    }

    /** @brief Destroys the `T` at `instance`: the `destroy` a description of `T` holds. */
    template <typename T>
    void DestroyValue(void* instance)
    {
        static_cast<T*>(instance)->~T();
    }

    /**
     * @brief The run-time description of one type: static, immutable data, held once per described type.
     *
     * A package's generated type registration holds one per type it describes and adds it to the type registry, and
     * `Describe<T>` yields the same object. Nothing copies a description: the registry, value references and other
     * descriptions point at it.
     */
    struct TypeDescription
    {
        /** @brief The canonical name, which files and the type registry name the type by (`Fenrir::Transform`). */
        std::string_view name;
        /** @brief The package whose registration describes the type (`FenrirECS`), which diagnostics name. */
        std::string_view package;
        /** @brief The type's role. */
        Role role = Role::SharedStruct;
        /** @brief The type's version (`@version N`), at least 1, for reading files written at older versions. */
        std::uint32_t version = 1;
        /** @brief The canonical names the type had before (`@formerly Old::Name`), which files written then spell. */
        std::span<const std::string_view> formerly;
        /** @brief `@transient`: the type is never written to a file (ADR-0078). */
        bool transient = false;
        /** @brief `@editor-only`: the type is authoring data for an editor only (ADR-0082). */
        bool editorOnly = false;
        /** @brief `sizeof` the type. */
        std::size_t size = 0;
        /** @brief `alignof` the type. */
        std::size_t alignment = 1;
        /** @brief Value-initialises an instance at an address of `size` bytes aligned to `alignment`. */
        void (*construct)(void* instance) = nullptr;
        /** @brief Destroys an instance `construct` made. */
        void (*destroy)(void* instance) = nullptr;
        /** @brief For a `DataAsset`: the canonical name of the one data asset type it extends; empty when none. */
        std::string_view base;
        /** @brief The type's doc comment, without its doc commands. */
        std::string_view doc;
        /** @brief Its fields in storage order; for a fixed-layout type, each with its offset. */
        std::span<const FieldInfo> fields;
        /** @brief For an `Enum`: its enumerators in declaration order. */
        std::span<const Enumerator> enumerators;
        /** @brief For an `Enum`: its underlying integer kind. */
        Kind underlying = Kind::I32;
        /**
         * @brief For a type holding an owning kind: the generated function that pushes each field of the instance at
         * `instance` to `visitor` once, its base's fields first (ADR-0075). Null for a fixed-layout type, which is
         * visited through its fields' offsets.
         */
        void (*visit)(void* instance, FieldVisitor& visitor) = nullptr;

        /** @brief Value-initialises an instance at `instance`, which has `size` bytes aligned to `alignment`. */
        void Construct(void* instance) const;

        /** @brief Destroys the instance at `instance`. */
        void Destroy(void* instance) const;

        /**
         * @brief Pushes each field of the instance at `instance` to `visitor` once, in storage order.
         *
         * Through `visit` when the type has one, else through each field's offset; a visitor cannot tell which. A
         * field of a nested struct arrives as one `Struct` value, which the visitor walks through `ValueRef::Visit`
         * if it wants its fields.
         */
        void Visit(void* instance, FieldVisitor& visitor) const;

        /** @brief Pushes each field of the instance at `instance` to `visitor` once, in storage order, read-only. */
        void Visit(const void* instance, ConstFieldVisitor& visitor) const;
    };
} // namespace Fenrir
