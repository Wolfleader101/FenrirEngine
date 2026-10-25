#include "FenrirAssetsScripting/AssetsScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetsScriptingState.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirMath/Colour.hpp"

// Everything C# sees of assets but the handles is generated (api/FenrirAssets.binding.toml): the shared structs' and
// enums' mappings, checked against the Fenrir.Assets contract whenever a script assembly loads, and from the native
// functions below (described into api/FenrirAssets.natives.json) their C# declarations, AssetsNative, and this
// registration of them all.
namespace Fenrir::Generated
{
    void RegisterAssets(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    namespace
    {
        // `count` values from `first` on, or nothing (with a warning) for a negative count or a null pointer with
        // values.
        template <typename T>
        std::optional<std::vector<T>> Copy(AssetsScripting& assets, const CallContext& context, const T* first,
                                           std::int32_t count, std::string_view function,
                                           std::source_location where = std::source_location::current())
        {
            if (count < 0 || (first == nullptr && count > 0))
            {
                assets.WarnOnce(context, std::string(function) + ": a span is unusable, so it does nothing", where);
                return std::nullopt;
            }
            // C# passes a span as its first element and its length, so the bounds are only as good as C#'s.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            const std::span<const T> values(first, static_cast<std::size_t>(count));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
            return std::vector<T>(values.begin(), values.end());
        }

        // A mesh from C#'s spans, drawn with one material: one submesh over every index. Takes the state first, so
        // describe-api does not read it as a native.
        std::optional<MeshData> MeshFrom(AssetsScripting& assets, const CallContext& context,
                                         const Math::Vec3* positions, std::int32_t positionCount,
                                         const Math::Vec3* normals, std::int32_t normalCount, const Math::Vec2* uvs,
                                         std::int32_t uvCount, const std::uint32_t* indices, std::int32_t indexCount,
                                         std::string_view function)
        {
            auto positionValues = Copy(assets, context, positions, positionCount, function);
            auto normalValues = Copy(assets, context, normals, normalCount, function);
            auto uvValues = Copy(assets, context, uvs, uvCount, function);
            auto indexValues = Copy(assets, context, indices, indexCount, function);
            if (!positionValues || !normalValues || !uvValues || !indexValues)
            {
                return std::nullopt;
            }
            MeshData mesh;
            mesh.positions = std::move(*positionValues);
            mesh.normals = std::move(*normalValues);
            mesh.uv0 = std::move(*uvValues);
            mesh.indices = std::move(*indexValues);
            mesh.submeshes = {{0, static_cast<std::uint32_t>(mesh.indices.size()), 0}};
            return mesh;
        }

        // An RGBA8 sRGB image of this size with `rgba8`'s bytes, or none as a render target; nothing (with a warning)
        // for a side under one pixel or an unusable span. Whether the bytes fit the size is the store's check.
        std::optional<ImageData> ImageFrom(AssetsScripting& assets, const CallContext& context, std::int32_t width,
                                           std::int32_t height, const std::uint8_t* rgba8, std::int32_t byteCount,
                                           std::string_view function,
                                           std::source_location where = std::source_location::current())
        {
            if (width < 1 || height < 1)
            {
                assets.WarnOnce(context,
                                std::string(function) + ": an image is at least one pixel each way, so it does nothing",
                                where);
                return std::nullopt;
            }
            auto bytes = Copy(assets, context, rgba8, byteCount, function, where);
            if (!bytes)
            {
                return std::nullopt;
            }
            ImageData image;
            image.width = static_cast<std::uint32_t>(width);
            image.height = static_cast<std::uint32_t>(height);
            image.format = ImageFormat::RGBA8;
            image.colourSpace = ColourSpace::Srgb;
            image.pixels.resize(bytes->size());
            std::transform(bytes->begin(), bytes->end(), image.pixels.begin(),
                           [](std::uint8_t value) { return std::byte{value}; });
            return image;
        }

        // Calls `action` with the asset kind C# names (an AssetKind<T>.Value), or warns once and gives `otherwise`.
        template <typename Result, typename Action>
        Result ForKind(AssetsScripting& assets, const CallContext& context, Detail::AssetKind kind,
                       std::string_view function, Result otherwise, Action&& action,
                       std::source_location where = std::source_location::current())
        {
            // Not a switch: `kind` comes from C#, so it may be none of the enumerators.
            if (kind == Detail::AssetKind::Mesh)
            {
                return action(AssetHandle<Mesh>{});
            }
            if (kind == Detail::AssetKind::Image)
            {
                return action(AssetHandle<Image>{});
            }
            if (kind == Detail::AssetKind::Material)
            {
                return action(AssetHandle<Material>{});
            }
            if (kind == Detail::AssetKind::Shader)
            {
                return action(AssetHandle<Shader>{});
            }
            if (kind == Detail::AssetKind::Model)
            {
                return action(AssetHandle<Model>{});
            }
            assets.WarnOnce(context,
                            std::string(function) + ": the handle's type is not Mesh, Image, Material, Shader or "
                                                    "Model, so it does nothing",
                            where);
            return otherwise;
        }

        // One material read: writes the default `Out{}` to `out`, then what `read` gives for a live material, if it
        // gives anything, and says whether it did. Warns once per call site for a stale material.
        template <typename Out, typename Read>
        bool ReadInto(AssetsScripting& assets, const CallContext& context, std::uint64_t material, Out* out,
                      std::string_view function, Read&& read,
                      std::source_location where = std::source_location::current())
        {
            if (out == nullptr)
            {
                return false;
            }
            *out = Out{};
            AssetStore* store = assets.Resolve<Material>(context, material, function, where);
            if (store == nullptr)
            {
                return false;
            }
            const std::optional<Out> value = read(*store, AssetsScripting::Unpack<Material>(material));
            if (!value)
            {
                return false;
            }
            *out = *value;
            return true;
        }
    } // namespace

    // The native functions behind Fenrir.Assets's Assets, Materials and Shaders, each named as C# calls it.
    // tools/describe-api reads them from this file, in this order, and Iris.Bindings generates their C# declarations
    // (Fenrir.Assets.AssetsNative) and their registration from that. A handle crosses as its packed bits (C#'s
    // AssetHandle<T>.Bits) and a kind as FenrirAssets's Detail::AssetKind; text as a view; spans as a pointer and a
    // count. Each returns doing nothing (false, zero or the empty handle) with no asset store, a stale or empty handle,
    // or an unusable argument, having written its output's default first. Not in an anonymous namespace: the generated
    // registration names them from another file.
    namespace AssetsNatives
    {
        /**
         * @brief Loads the asset of `kind` at `path` for `owner` (AssetStore::Load).
         * @return The asset's handle, or the empty handle if the load failed (the store logs why).
         */
        std::uint64_t Load(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind,
                           std::string_view path, ScriptAssetOwner owner)
        {
            AssetStore* store = assets.StoreFor(context, "Assets.Load");
            const std::optional<AssetOwner> assetOwner =
                store ? assets.OwnerOf(context, owner, "Assets.Load") : std::nullopt;
            if (!assetOwner)
            {
                return 0;
            }
            return ForKind(assets, context, kind, "Assets.Load", std::uint64_t{0}, [&]<typename T>(AssetHandle<T>) {
                return AssetsScripting::Pack(store->Load<T>(path, *assetOwner).handle);
            });
        }

        /**
         * @brief Frees the asset of `kind` now, whoever owns it.
         * @return Whether the handle named a live asset.
         */
        bool Unload(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind, std::uint64_t handle)
        {
            return ForKind(assets, context, kind, "Assets.Unload", false, [&]<typename T>(AssetHandle<T>) {
                AssetStore* store = assets.Resolve<T>(context, handle, "Assets.Unload");
                return store != nullptr && store->Unload(AssetsScripting::Unpack<T>(handle));
            });
        }

        /** @brief Whether `handle` names a live asset of `kind`. */
        bool IsValid(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind, std::uint64_t handle)
        {
            AssetStore* store = assets.StoreFor(context, "Assets.IsValid");
            return store != nullptr &&
                   ForKind(assets, context, kind, "Assets.IsValid", false, [&]<typename T>(AssetHandle<T>) {
                       return store->IsValid(AssetsScripting::Unpack<T>(handle));
                   });
        }

        /**
         * @brief Adds a mesh of these vertex streams and triangles for `owner`, drawn with one material.
         * @return The mesh, or the empty handle if the store refused it.
         */
        std::uint64_t AddMesh(const CallContext& context, AssetsScripting& assets, const Math::Vec3* positions,
                              std::int32_t positionCount, const Math::Vec3* normals, std::int32_t normalCount,
                              const Math::Vec2* uvs, std::int32_t uvCount, const std::uint32_t* indices,
                              std::int32_t indexCount, ScriptAssetOwner owner)
        {
            AssetStore* store = assets.StoreFor(context, "Assets.AddMesh");
            const std::optional<AssetOwner> assetOwner =
                store ? assets.OwnerOf(context, owner, "Assets.AddMesh") : std::nullopt;
            if (!assetOwner)
            {
                return 0;
            }
            std::optional<MeshData> mesh = MeshFrom(assets, context, positions, positionCount, normals, normalCount,
                                                    uvs, uvCount, indices, indexCount, "Assets.AddMesh");
            return mesh ? AssetsScripting::Pack(store->Add<Mesh>(std::move(*mesh), *assetOwner)) : 0;
        }

        /**
         * @brief Swaps the mesh's data for these streams and triangles.
         * @return Whether it was replaced.
         */
        bool ReplaceMesh(const CallContext& context, AssetsScripting& assets, std::uint64_t mesh,
                         const Math::Vec3* positions, std::int32_t positionCount, const Math::Vec3* normals,
                         std::int32_t normalCount, const Math::Vec2* uvs, std::int32_t uvCount,
                         const std::uint32_t* indices, std::int32_t indexCount)
        {
            AssetStore* store = assets.Resolve<Mesh>(context, mesh, "Assets.ReplaceMesh");
            if (store == nullptr)
            {
                return false;
            }
            std::optional<MeshData> data = MeshFrom(assets, context, positions, positionCount, normals, normalCount,
                                                    uvs, uvCount, indices, indexCount, "Assets.ReplaceMesh");
            return data && store->Replace(AssetsScripting::Unpack<Mesh>(mesh), std::move(*data));
        }

        /**
         * @brief Adds a render target for `owner`: an RGBA8 sRGB image of this size with no pixels.
         * @return The image, or the empty handle if either side is under one pixel.
         */
        std::uint64_t AddRenderTarget(const CallContext& context, AssetsScripting& assets, std::int32_t width,
                                      std::int32_t height, ScriptAssetOwner owner)
        {
            AssetStore* store = assets.StoreFor(context, "Assets.AddRenderTarget");
            const std::optional<AssetOwner> assetOwner =
                store ? assets.OwnerOf(context, owner, "Assets.AddRenderTarget") : std::nullopt;
            if (!assetOwner)
            {
                return 0;
            }
            std::optional<ImageData> image =
                ImageFrom(assets, context, width, height, nullptr, 0, "Assets.AddRenderTarget");
            return image ? AssetsScripting::Pack(store->Add<Image>(std::move(*image), *assetOwner)) : 0;
        }

        /**
         * @brief Gives the render target a new size.
         * @return Whether it was resized.
         */
        bool ResizeRenderTarget(const CallContext& context, AssetsScripting& assets, std::uint64_t image,
                                std::int32_t width, std::int32_t height)
        {
            AssetStore* store = assets.Resolve<Image>(context, image, "Assets.ResizeRenderTarget");
            if (store == nullptr)
            {
                return false;
            }
            if (!store->TryGet(AssetsScripting::Unpack<Image>(image))->pixels.empty())
            {
                assets.WarnOnce(context, "Assets.ResizeRenderTarget: the image has pixels, so it is not a render "
                                         "target and it does nothing");
                return false;
            }
            std::optional<ImageData> data =
                ImageFrom(assets, context, width, height, nullptr, 0, "Assets.ResizeRenderTarget");
            return data && store->Replace(AssetsScripting::Unpack<Image>(image), std::move(*data));
        }

        /**
         * @brief Adds an RGBA8 sRGB image of this size from `rgba8` for `owner`.
         * @return The image, or the empty handle if the store refused it.
         */
        std::uint64_t AddImage(const CallContext& context, AssetsScripting& assets, std::int32_t width,
                               std::int32_t height, const std::uint8_t* rgba8, std::int32_t byteCount,
                               ScriptAssetOwner owner)
        {
            AssetStore* store = assets.StoreFor(context, "Assets.AddImage");
            const std::optional<AssetOwner> assetOwner =
                store ? assets.OwnerOf(context, owner, "Assets.AddImage") : std::nullopt;
            if (!assetOwner)
            {
                return 0;
            }
            std::optional<ImageData> image =
                ImageFrom(assets, context, width, height, rgba8, byteCount, "Assets.AddImage");
            return image ? AssetsScripting::Pack(store->Add<Image>(std::move(*image), *assetOwner)) : 0;
        }

        /**
         * @brief Swaps the image's data for an RGBA8 sRGB image from `rgba8`.
         * @return Whether it was replaced.
         */
        bool ReplaceImage(const CallContext& context, AssetsScripting& assets, std::uint64_t image, std::int32_t width,
                          std::int32_t height, const std::uint8_t* rgba8, std::int32_t byteCount)
        {
            AssetStore* store = assets.Resolve<Image>(context, image, "Assets.ReplaceImage");
            if (store == nullptr)
            {
                return false;
            }
            std::optional<ImageData> data =
                ImageFrom(assets, context, width, height, rgba8, byteCount, "Assets.ReplaceImage");
            return data && store->Replace(AssetsScripting::Unpack<Image>(image), std::move(*data));
        }

        /**
         * @brief A new material drawing with `shader`, for `owner` (AssetStore::CreateMaterial).
         * @return The material, or the empty handle if the shader does not resolve.
         */
        std::uint64_t CreateMaterial(const CallContext& context, AssetsScripting& assets, std::uint64_t shader,
                                     ScriptAssetOwner owner)
        {
            AssetStore* store = assets.Resolve<Shader>(context, shader, "Materials.Create");
            const std::optional<AssetOwner> assetOwner =
                store ? assets.OwnerOf(context, owner, "Materials.Create") : std::nullopt;
            return assetOwner ? AssetsScripting::Pack(
                                    store->CreateMaterial(AssetsScripting::Unpack<Shader>(shader), *assetOwner))
                              : 0;
        }

        /**
         * @brief A copy of `material` for `owner` (AssetStore::CloneMaterial).
         * @return The clone, or the empty handle if the material does not resolve.
         */
        std::uint64_t CloneMaterial(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                    ScriptAssetOwner owner)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.Clone");
            const std::optional<AssetOwner> assetOwner =
                store ? assets.OwnerOf(context, owner, "Materials.Clone") : std::nullopt;
            return assetOwner ? AssetsScripting::Pack(
                                    store->CloneMaterial(AssetsScripting::Unpack<Material>(material), *assetOwner))
                              : 0;
        }

        /** @brief Sets a `float` parameter (AssetStore::SetFloat). */
        bool SetFloat(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                      std::string_view name, float value)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetFloat");
            return store != nullptr && store->SetFloat(AssetsScripting::Unpack<Material>(material), name, value);
        }

        /** @brief Sets a `vec2` parameter (AssetStore::SetVector2). */
        bool SetVector2(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, const Math::Vec2* value)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetVector2");
            return store != nullptr && value != nullptr &&
                   store->SetVector2(AssetsScripting::Unpack<Material>(material), name, *value);
        }

        /** @brief Sets a `vec3` parameter (AssetStore::SetVector3). */
        bool SetVector3(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, const Math::Vec3* value)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetVector3");
            return store != nullptr && value != nullptr &&
                   store->SetVector3(AssetsScripting::Unpack<Material>(material), name, *value);
        }

        /** @brief Sets a `vec4` parameter (AssetStore::SetVector4). */
        bool SetVector4(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, const Math::Vec4* value)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetVector4");
            return store != nullptr && value != nullptr &&
                   store->SetVector4(AssetsScripting::Unpack<Material>(material), name, *value);
        }

        /** @brief Sets a `vec4` parameter to a colour (AssetStore::SetColour). */
        bool SetColour(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                       std::string_view name, const Colour* value)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetColour");
            return store != nullptr && value != nullptr &&
                   store->SetColour(AssetsScripting::Unpack<Material>(material), name, *value);
        }

        /** @brief Sets a texture parameter to `image`, read with `sampler` (AssetStore::SetTexture). */
        bool SetTexture(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, std::uint64_t image, const SamplerDescription* sampler)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetTexture");
            return store != nullptr && sampler != nullptr &&
                   store->SetTexture(AssetsScripting::Unpack<Material>(material), name,
                                     AssetsScripting::Unpack<Image>(image), *sampler);
        }

        /** @brief Sets how the material's pixels combine with what is behind them (AssetStore::SetBlendMode). */
        bool SetBlendMode(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                          BlendMode blendMode)
        {
            // `blendMode` comes from C#, so it may be none of the enumerators.
            if (blendMode > BlendMode::Additive)
            {
                assets.WarnOnce(context, "Materials.SetBlendMode: no such blend mode, so it does nothing");
                return false;
            }
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetBlendMode");
            return store != nullptr && store->SetBlendMode(AssetsScripting::Unpack<Material>(material), blendMode);
        }

        /** @brief Sets the alpha below which a masked material discards a pixel (AssetStore::SetAlphaCutoff). */
        bool SetAlphaCutoff(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                            float alphaCutoff)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetAlphaCutoff");
            return store != nullptr && store->SetAlphaCutoff(AssetsScripting::Unpack<Material>(material), alphaCutoff);
        }

        /** @brief Sets whether both faces of the material are drawn (AssetStore::SetDoubleSided). */
        bool SetDoubleSided(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                            bool doubleSided)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.SetDoubleSided");
            return store != nullptr && store->SetDoubleSided(AssetsScripting::Unpack<Material>(material), doubleSided);
        }

        /**
         * @brief Reads a `float` parameter's effective value (AssetStore::TryGetFloat).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetFloat(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                         std::string_view name, float* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetFloat",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetFloat(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec2` parameter's effective value (AssetStore::TryGetVector2).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetVector2(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                           std::string_view name, Math::Vec2* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetVector2",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetVector2(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec3` parameter's effective value (AssetStore::TryGetVector3).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetVector3(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                           std::string_view name, Math::Vec3* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetVector3",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetVector3(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec4` parameter's effective value (AssetStore::TryGetVector4).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetVector4(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                           std::string_view name, Math::Vec4* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetVector4",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetVector4(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec4` parameter's effective value as a colour (AssetStore::TryGetColour).
         * @param[out] value The value; the default (white) when there is none.
         * @return Whether there was one.
         */
        bool TryGetColour(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                          std::string_view name, Colour* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetColour",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetColour(handle, name);
                            });
        }

        /**
         * @brief Reads a texture parameter's effective value (AssetStore::TryGetTexture).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetTexture(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                           std::string_view name, MaterialTexture* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetTexture",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetTexture(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `float` override (AssetStore::TryGetOverrideFloat).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetOverrideFloat(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                 std::string_view name, float* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetOverrideFloat",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideFloat(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec2` override (AssetStore::TryGetOverrideVector2).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetOverrideVector2(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                   std::string_view name, Math::Vec2* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetOverrideVector2",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideVector2(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec3` override (AssetStore::TryGetOverrideVector3).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetOverrideVector3(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                   std::string_view name, Math::Vec3* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetOverrideVector3",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideVector3(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec4` override (AssetStore::TryGetOverrideVector4).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetOverrideVector4(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                   std::string_view name, Math::Vec4* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetOverrideVector4",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideVector4(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec4` override as a colour (AssetStore::TryGetOverrideColour).
         * @param[out] value The override; the default (white) when there is none.
         * @return Whether there was one.
         */
        bool TryGetOverrideColour(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                  std::string_view name, Colour* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetOverrideColour",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideColour(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own texture override (AssetStore::TryGetOverrideTexture).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetOverrideTexture(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                   std::string_view name, MaterialTexture* value)
        {
            return ReadInto(assets, context, material, value, "Materials.TryGetOverrideTexture",
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideTexture(handle, name);
                            });
        }

        /** @brief Whether the material has an override of the parameter (AssetStore::IsSet). */
        bool IsSet(const CallContext& context, AssetsScripting& assets, std::uint64_t material, std::string_view name)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.IsSet");
            return store != nullptr && store->IsSet(AssetsScripting::Unpack<Material>(material), name);
        }

        /**
         * @brief Removes the material's override of the parameter (AssetStore::Reset).
         * @return Whether there was one to remove.
         */
        bool Reset(const CallContext& context, AssetsScripting& assets, std::uint64_t material, std::string_view name)
        {
            AssetStore* store = assets.Resolve<Material>(context, material, "Materials.Reset");
            return store != nullptr && store->Reset(AssetsScripting::Unpack<Material>(material), name);
        }

        /** @brief How many material parameters the shader has (AssetStore::GetShaderParameters). */
        std::int32_t GetShaderParameterCount(const CallContext& context, AssetsScripting& assets, std::uint64_t shader)
        {
            AssetStore* store = assets.Resolve<Shader>(context, shader, "Shaders.GetParameterCount");
            return store != nullptr ? static_cast<std::int32_t>(
                                          store->GetShaderParameters(AssetsScripting::Unpack<Shader>(shader)).size())
                                    : 0;
        }

        /**
         * @brief Reads parameter `index` of the shader: its type and default, and as much of its name as `capacity`
         * chars from `name` on hold.
         * @param[out] parameter The parameter without its name; the default when there is none.
         * @return The name's whole length in chars, or zero if there is no such parameter.
         */
        std::int32_t GetShaderParameter(const CallContext& context, AssetsScripting& assets, std::uint64_t shader,
                                        std::int32_t index, ScriptShaderParameter* parameter, char16_t* name,
                                        std::int32_t capacity)
        {
            if (parameter != nullptr)
            {
                *parameter = ScriptShaderParameter{};
            }
            AssetStore* store = assets.Resolve<Shader>(context, shader, "Shaders.GetParameter");
            if (store == nullptr)
            {
                return 0;
            }
            const std::span<const ShaderParameter> parameters =
                store->GetShaderParameters(AssetsScripting::Unpack<Shader>(shader));
            if (index < 0 || static_cast<std::size_t>(index) >= parameters.size())
            {
                assets.WarnOnce(context, "Shaders.GetParameter: the shader has no parameter at that index, so it does "
                                         "nothing");
                return 0;
            }
            const ShaderParameter& found = parameters[static_cast<std::size_t>(index)];
            if (parameter != nullptr)
            {
                *parameter = {
                    found.type, {found.value[0], found.value[1], found.value[2], found.value[3]}, found.texture};
            }
            // GLSL names are ASCII, so each byte is one UTF-16 char.
            const std::size_t room = name == nullptr || capacity < 0 ? 0 : static_cast<std::size_t>(capacity);
            const std::string_view copied = std::string_view(found.name).substr(0, room);
            std::transform(copied.begin(), copied.end(), name,
                           [](char c) { return static_cast<char16_t>(static_cast<unsigned char>(c)); });
            return static_cast<std::int32_t>(found.name.size());
        }
    } // namespace AssetsNatives

    Fenrir::ScriptingExtension Assets::ScriptingExtension()
    {
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirAssets";
        extension.SetState(std::make_shared<AssetsScripting>());
        extension.sharedAssemblies = {"Fenrir.Assets.dll"};
        Generated::RegisterAssets(extension);
        return extension;
    }
} // namespace Fenrir
