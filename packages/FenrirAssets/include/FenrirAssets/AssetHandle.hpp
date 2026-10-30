#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fenrir
{
    /** @name Asset names
     * The short names an asset handle is typed by (`AssetHandle<Mesh>`), matching C#'s marker types. Each names a kind
     * of asset and is never made. `Model` is also the data a model handle resolves to.
     * @{
     */
    struct Mesh;
    struct Image;
    struct Material;
    struct Shader;
    struct Model;
    struct Skeleton;
    struct Clip;
    struct BoneMask;
    /** @} */

    struct MeshData;
    struct ImageData;
    struct MaterialData;
    struct ShaderData;
    struct SkeletonData;
    struct ClipData;
    struct BoneMaskData;

    namespace Detail
    {
        /** @brief Each kind of asset the store holds, one slot table per kind. */
        enum class AssetKind : std::uint8_t
        {
            Mesh,
            Image,
            Material,
            Shader,
            Model,
            Skeleton,
            Clip,
            BoneMask,
        };

        /** @brief How many kinds of asset the store holds. */
        inline constexpr std::size_t AssetKindCount = 8;

        /** @brief What an asset name resolves to: its data type, its kind and the word log messages use for it. */
        template <typename T>
        struct AssetTraits;

        template <>
        struct AssetTraits<Mesh>
        {
            using Data = MeshData;
            static constexpr AssetKind kind = AssetKind::Mesh;
            static constexpr std::string_view name = "mesh";
        };

        template <>
        struct AssetTraits<Image>
        {
            using Data = ImageData;
            static constexpr AssetKind kind = AssetKind::Image;
            static constexpr std::string_view name = "image";
        };

        template <>
        struct AssetTraits<Material>
        {
            using Data = MaterialData;
            static constexpr AssetKind kind = AssetKind::Material;
            static constexpr std::string_view name = "material";
        };

        template <>
        struct AssetTraits<Shader>
        {
            using Data = ShaderData;
            static constexpr AssetKind kind = AssetKind::Shader;
            static constexpr std::string_view name = "shader";
        };

        template <>
        struct AssetTraits<Model>
        {
            using Data = Model;
            static constexpr AssetKind kind = AssetKind::Model;
            static constexpr std::string_view name = "model";
        };

        template <>
        struct AssetTraits<Skeleton>
        {
            using Data = SkeletonData;
            static constexpr AssetKind kind = AssetKind::Skeleton;
            static constexpr std::string_view name = "skeleton";
        };

        template <>
        struct AssetTraits<Clip>
        {
            using Data = ClipData;
            static constexpr AssetKind kind = AssetKind::Clip;
            static constexpr std::string_view name = "clip";
        };

        template <>
        struct AssetTraits<BoneMask>
        {
            using Data = BoneMaskData;
            static constexpr AssetKind kind = AssetKind::BoneMask;
            static constexpr std::string_view name = "bone mask";
        };
    } // namespace Detail

    /** @brief The data an asset handle of `T` resolves to: `AssetData<Mesh>` is `MeshData`. */
    template <typename T>
    using AssetData = typename Detail::AssetTraits<T>::Data;

    /**
     * @brief Names one asset of kind `T` (`Mesh`, `Image`, `Material`, `Shader`, `Model`, `Skeleton`, `Clip` or
     * `BoneMask`) in the asset store: a slot index plus the generation the slot had when the asset was made (ADR-0021).
     * Plain data that keeps nothing alive, so components hold it and C# receives it as a value. Once the asset is freed
     * the slot's generation moves on and the handle is stale: it no longer resolves, and users skip it. A default-made
     * handle is empty and never resolves.
     */
    template <typename T>
    struct AssetHandle
    {
        /** @brief The slot in the store's table of `T`. */
        std::uint32_t index = 0;
        /** @brief The slot's generation when the asset was made; zero only in an empty handle. */
        std::uint32_t generation = 0;

        /** @brief Whether this is the empty handle, which names no asset. A handle that is not empty may be stale. */
        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return generation == 0;
        }

        friend constexpr bool operator==(const AssetHandle&, const AssetHandle&) = default;
    };
} // namespace Fenrir
