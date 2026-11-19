#pragma once

#include <cstdint>
#include <string_view>

namespace Fenrir
{
    /**
     * @brief What a described value is: the API description's kinds (ADR-0074).
     *
     * The plain kinds hold bytes at an offset and are allowed in every role. `Reference`, `Entity` and `FixedString`
     * are fixed-layout too. `String`, `List`, `Optional` and `Path` are owning: their bytes are a standard library
     * object's, reached only through a value reference, and only the `DataAsset` and `Settings` roles admit them.
     */
    enum class Kind : std::uint8_t
    {
        I8,
        I16,
        I32,
        I64,
        U8,
        U16,
        U32,
        U64,
        F32,
        F64,
        Bool,
        /** @brief A UTF-16 code unit, `char16_t`. */
        Char16,
        /** @brief An enum of its underlying integer kind, described by its own type description. */
        Enum,
        /** @brief A described struct, tagged union or bound type, held by value. */
        Struct,
        /** @brief A fixed count of contiguous elements of one kind; never an array of arrays. */
        Array,
        /** @brief Eight opaque bytes: any pointer, its target never described. */
        Pointer,
        /** @brief Owning: a `std::string` of UTF-8 text. */
        String,
        /** @brief Owning: a `std::vector<T>` of elements of one kind, a list included. */
        List,
        /** @brief Owning: a `std::optional<T>` of any kind but an optional. */
        Optional,
        /** @brief An asset handle, `AssetHandle<T>`: an index and a generation, naming its target type. */
        Reference,
        /** @brief A `Fenrir::Entity`: 16 bytes, an entity id and the entity list it belongs to. */
        Entity,
        /** @brief A `char[N]` of UTF-8 text, written as text and read back cut at N-1 bytes on a UTF-8 boundary. */
        FixedString,
        /** @brief Owning: a `std::filesystem::path`. */
        Path,
    };

    /** @brief The role a described type plays, as the API description records it. */
    enum class Role : std::uint8_t
    {
        Component,
        Event,
        SceneResource,
        AppResource,
        SharedStruct,
        BoundType,
        /** @brief A type described only because a field of another type uses it. */
        Nested,
        /** @brief An enumeration: its description carries enumerators and no fields. */
        Enum,
        /** @brief A game-defined plain struct in the asset store, which may extend one other data asset type. */
        DataAsset,
        /** @brief An `Install` config or an importer's options type. */
        Settings,
    };

    /** @brief Whether values of this kind own memory, so are reached only through a value reference, never by offset.
     */
    [[nodiscard]] constexpr bool IsOwning(Kind kind) noexcept
    {
        return kind == Kind::String || kind == Kind::List || kind == Kind::Optional || kind == Kind::Path;
    }

    /** @brief Whether types in this role may hold fields of owning kinds: data assets and settings alone do. */
    [[nodiscard]] constexpr bool AdmitsOwningKinds(Role role) noexcept
    {
        return role == Role::DataAsset || role == Role::Settings;
    }

    /** @brief The kind's name as the API description spells it (`i32`, `fixed-string`). */
    [[nodiscard]] std::string_view KindName(Kind kind) noexcept;

    /** @brief The role's name as the API description spells it (`scene-resource`, `data-asset`). */
    [[nodiscard]] std::string_view RoleName(Role role) noexcept;
} // namespace Fenrir
