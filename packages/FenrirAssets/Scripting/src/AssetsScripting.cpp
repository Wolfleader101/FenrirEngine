#include "FenrirAssetsScripting/AssetsScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetsScriptingState.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirScripting/Enumerator.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "Generated/FenrirAssetsNatives.hpp"

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
        // A mesh from C#'s spans (each empty when C# passed none), drawn with one material: one submesh over every
        // index.
        MeshData MeshFrom(const Math::Vec3* positions, std::int32_t positionCount, const Math::Vec3* normals,
                          std::int32_t normalCount, const Math::Vec2* uvs, std::int32_t uvCount,
                          const std::uint32_t* indices, std::int32_t indexCount)
        {
            const std::span<const Math::Vec3> positionValues = SpanOf(positions, positionCount);
            const std::span<const Math::Vec3> normalValues = SpanOf(normals, normalCount);
            const std::span<const Math::Vec2> uvValues = SpanOf(uvs, uvCount);
            const std::span<const std::uint32_t> indexValues = SpanOf(indices, indexCount);
            MeshData mesh;
            mesh.positions.assign(positionValues.begin(), positionValues.end());
            mesh.normals.assign(normalValues.begin(), normalValues.end());
            mesh.uv0.assign(uvValues.begin(), uvValues.end());
            mesh.indices.assign(indexValues.begin(), indexValues.end());
            mesh.submeshes = {{0, static_cast<std::uint32_t>(mesh.indices.size()), 0}};
            return mesh;
        }

        // An RGBA8 sRGB image of this size with `rgba8`'s bytes, or none as a render target; nothing (with a warning)
        // for a side under one pixel. Whether the bytes fit the size is the store's check.
        std::optional<ImageData> ImageFrom(const NativeGuard& guard, std::int32_t width, std::int32_t height,
                                           const std::uint8_t* rgba8, std::int32_t byteCount,
                                           std::source_location where = std::source_location::current())
        {
            if (width < 1 || height < 1)
            {
                guard.Warn("an image is at least one pixel each way, so it does nothing", where);
                return std::nullopt;
            }
            const std::span<const std::uint8_t> bytes = SpanOf(rgba8, byteCount);
            ImageData image;
            image.width = static_cast<std::uint32_t>(width);
            image.height = static_cast<std::uint32_t>(height);
            image.format = ImageFormat::RGBA8;
            image.colourSpace = ColourSpace::Srgb;
            image.pixels.resize(bytes.size());
            std::transform(bytes.begin(), bytes.end(), image.pixels.begin(),
                           [](std::uint8_t value) { return std::byte{value}; });
            return image;
        }

        // Calls `action` with the asset kind C# names (an AssetKind<T>.Value), or warns once and gives `otherwise`.
        template <typename Result, typename Action>
        Result ForKind(const NativeGuard& guard, Detail::AssetKind kind, Result otherwise, Action&& action,
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
            guard.Warn("the handle's type is not Mesh, Image, Material, Shader or Model, so it does nothing", where);
            return otherwise;
        }

        // One material read: writes what `read` gives for a live material to `out`, if it gives anything, and says
        // whether it did. Warns once per call site for a stale material; `out` holds its default until written (the
        // registration zeroes it, ADR-0045).
        template <typename Out, typename Read>
        bool ReadInto(const NativeGuard& guard, std::uint64_t material, Out* out, Read&& read,
                      std::source_location where = std::source_location::current())
        {
            AssetStore* store = Resolve<Material>(guard, material, where);
            if (store == nullptr)
            {
                return false;
            }
            const std::optional<Out> value = read(*store, AssetHandleFromBits<Material>(material));
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
    // or an unusable argument, warning once per place under its own name through its guard; its outputs hold their
    // defaults because the registration zeroed them (ADR-0045). Not in an anonymous namespace: the generated
    // natives header declares them.
    namespace AssetsNatives
    {
        /**
         * @brief Loads the asset of `kind` at `path` for `owner` (AssetStore::Load).
         * @return The asset's handle, or the empty handle if the load failed (the store logs why).
         */
        std::uint64_t Load(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind,
                           std::string_view path, ScriptAssetOwner owner)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return 0;
            }
            return ForKind(guard, kind, std::uint64_t{0}, [&]<typename T>(AssetHandle<T>) {
                return AssetHandleBits(placed->store.Load<T>(path, placed->owner).handle);
            });
        }

        /**
         * @brief Frees the asset of `kind` now, whoever owns it.
         * @return Whether the handle named a live asset.
         */
        bool Unload(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind, std::uint64_t handle)
        {
            const NativeGuard guard(context, assets.warnings);
            return ForKind(guard, kind, false, [&]<typename T>(AssetHandle<T>) {
                AssetStore* store = Resolve<T>(guard, handle);
                return store != nullptr && store->Unload(AssetHandleFromBits<T>(handle));
            });
        }

        /** @brief Whether `handle` names a live asset of `kind`. */
        bool IsValid(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind, std::uint64_t handle)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = StoreFor(guard);
            return store != nullptr && ForKind(guard, kind, false, [&]<typename T>(AssetHandle<T>) {
                       return store->IsValid(AssetHandleFromBits<T>(handle));
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
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return 0;
            }
            return AssetHandleBits(placed->store.Add<Mesh>(
                MeshFrom(positions, positionCount, normals, normalCount, uvs, uvCount, indices, indexCount),
                placed->owner));
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
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Mesh>(guard, mesh);
            return store != nullptr && store->Replace(AssetHandleFromBits<Mesh>(mesh),
                                                      MeshFrom(positions, positionCount, normals, normalCount, uvs,
                                                               uvCount, indices, indexCount));
        }

        /**
         * @brief Adds a render target for `owner`: an RGBA8 sRGB image of this size with no pixels.
         * @return The image, or the empty handle if either side is under one pixel.
         */
        std::uint64_t AddRenderTarget(const CallContext& context, AssetsScripting& assets, std::int32_t width,
                                      std::int32_t height, ScriptAssetOwner owner)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return 0;
            }
            std::optional<ImageData> image = ImageFrom(guard, width, height, nullptr, 0);
            return image ? AssetHandleBits(placed->store.Add<Image>(std::move(*image), placed->owner)) : 0;
        }

        /**
         * @brief Gives the render target a new size.
         * @return Whether it was resized.
         */
        bool ResizeRenderTarget(const CallContext& context, AssetsScripting& assets, std::uint64_t image,
                                std::int32_t width, std::int32_t height)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Image>(guard, image);
            if (store == nullptr)
            {
                return false;
            }
            if (!store->TryGet(AssetHandleFromBits<Image>(image))->pixels.empty())
            {
                guard.Warn("the image has pixels, so it is not a render target and it does nothing");
                return false;
            }
            std::optional<ImageData> data = ImageFrom(guard, width, height, nullptr, 0);
            return data && store->Replace(AssetHandleFromBits<Image>(image), std::move(*data));
        }

        /**
         * @brief Adds an RGBA8 sRGB image of this size from `rgba8` for `owner`.
         * @return The image, or the empty handle if the store refused it.
         */
        std::uint64_t AddImage(const CallContext& context, AssetsScripting& assets, std::int32_t width,
                               std::int32_t height, const std::uint8_t* rgba8, std::int32_t byteCount,
                               ScriptAssetOwner owner)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return 0;
            }
            std::optional<ImageData> image = ImageFrom(guard, width, height, rgba8, byteCount);
            return image ? AssetHandleBits(placed->store.Add<Image>(std::move(*image), placed->owner)) : 0;
        }

        /**
         * @brief Swaps the image's data for an RGBA8 sRGB image from `rgba8`.
         * @return Whether it was replaced.
         */
        bool ReplaceImage(const CallContext& context, AssetsScripting& assets, std::uint64_t image, std::int32_t width,
                          std::int32_t height, const std::uint8_t* rgba8, std::int32_t byteCount)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Image>(guard, image);
            if (store == nullptr)
            {
                return false;
            }
            std::optional<ImageData> data = ImageFrom(guard, width, height, rgba8, byteCount);
            return data && store->Replace(AssetHandleFromBits<Image>(image), std::move(*data));
        }

        /**
         * @brief A new material drawing with `shader`, for `owner` (AssetStore::CreateMaterial).
         * @return The material, or the empty handle if the shader does not resolve.
         */
        std::uint64_t CreateMaterial(const CallContext& context, AssetsScripting& assets, std::uint64_t shader,
                                     ScriptAssetOwner owner)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed =
                assets.StoreAndOwnerFor<Shader>(guard, shader, owner);
            return placed ? AssetHandleBits(
                                placed->store.CreateMaterial(AssetHandleFromBits<Shader>(shader), placed->owner))
                          : 0;
        }

        /**
         * @brief A copy of `material` for `owner` (AssetStore::CloneMaterial).
         * @return The clone, or the empty handle if the material does not resolve.
         */
        std::uint64_t CloneMaterial(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                                    ScriptAssetOwner owner)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed =
                assets.StoreAndOwnerFor<Material>(guard, material, owner);
            return placed ? AssetHandleBits(
                                placed->store.CloneMaterial(AssetHandleFromBits<Material>(material), placed->owner))
                          : 0;
        }

        /** @brief Sets a `float` parameter (AssetStore::SetFloat). */
        bool SetFloat(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                      std::string_view name, float value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetFloat(AssetHandleFromBits<Material>(material), name, value);
        }

        /** @brief Sets a `vec2` parameter (AssetStore::SetVector2). */
        bool SetVector2(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, const Math::Vec2* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetVector2(AssetHandleFromBits<Material>(material), name, *value);
        }

        /** @brief Sets a `vec3` parameter (AssetStore::SetVector3). */
        bool SetVector3(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, const Math::Vec3* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetVector3(AssetHandleFromBits<Material>(material), name, *value);
        }

        /** @brief Sets a `vec4` parameter (AssetStore::SetVector4). */
        bool SetVector4(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, const Math::Vec4* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetVector4(AssetHandleFromBits<Material>(material), name, *value);
        }

        /** @brief Sets a `vec4` parameter to a colour (AssetStore::SetColour). */
        bool SetColour(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                       std::string_view name, const Colour* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetColour(AssetHandleFromBits<Material>(material), name, *value);
        }

        /** @brief Sets a texture parameter to `image`, read with `sampler` (AssetStore::SetTexture). */
        bool SetTexture(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                        std::string_view name, std::uint64_t image, const SamplerDescription* sampler)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetTexture(AssetHandleFromBits<Material>(material), name,
                                                         AssetHandleFromBits<Image>(image), *sampler);
        }

        /** @brief Sets how the material's pixels combine with what is behind them (AssetStore::SetBlendMode). */
        bool SetBlendMode(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                          BlendMode blendMode)
        {
            const NativeGuard guard(context, assets.warnings);
            if (!IsEnumerator<BlendMode::Additive>(blendMode))
            {
                guard.Warn("no such blend mode, so it does nothing");
                return false;
            }
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetBlendMode(AssetHandleFromBits<Material>(material), blendMode);
        }

        /** @brief Sets the alpha below which a masked material discards a pixel (AssetStore::SetAlphaCutoff). */
        bool SetAlphaCutoff(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                            float alphaCutoff)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetAlphaCutoff(AssetHandleFromBits<Material>(material), alphaCutoff);
        }

        /** @brief Sets whether both faces of the material are drawn (AssetStore::SetDoubleSided). */
        bool SetDoubleSided(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                            bool doubleSided)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->SetDoubleSided(AssetHandleFromBits<Material>(material), doubleSided);
        }

        /**
         * @brief Reads a `float` parameter's effective value (AssetStore::TryGetFloat).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         */
        bool TryGetFloat(const CallContext& context, AssetsScripting& assets, std::uint64_t material,
                         std::string_view name, float* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
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
            return ReadInto(NativeGuard(context, assets.warnings), material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideTexture(handle, name);
                            });
        }

        /** @brief Whether the material has an override of the parameter (AssetStore::IsSet). */
        bool IsSet(const CallContext& context, AssetsScripting& assets, std::uint64_t material, std::string_view name)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->IsSet(AssetHandleFromBits<Material>(material), name);
        }

        /**
         * @brief Removes the material's override of the parameter (AssetStore::Reset).
         * @return Whether there was one to remove.
         */
        bool Reset(const CallContext& context, AssetsScripting& assets, std::uint64_t material, std::string_view name)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, material);
            return store != nullptr && store->Reset(AssetHandleFromBits<Material>(material), name);
        }

        /** @brief How many material parameters the shader has (AssetStore::GetShaderParameters). */
        std::int32_t GetShaderParameterCount(const CallContext& context, AssetsScripting& assets, std::uint64_t shader)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Shader>(guard, shader);
            return store != nullptr ? static_cast<std::int32_t>(
                                          store->GetShaderParameters(AssetHandleFromBits<Shader>(shader)).size())
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
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Shader>(guard, shader);
            if (store == nullptr)
            {
                return 0;
            }
            const std::span<const ShaderParameter> parameters =
                store->GetShaderParameters(AssetHandleFromBits<Shader>(shader));
            if (index < 0 || static_cast<std::size_t>(index) >= parameters.size())
            {
                guard.Warn("the shader has no parameter at that index, so it does nothing");
                return 0;
            }
            const ShaderParameter& found = parameters[static_cast<std::size_t>(index)];
            *parameter = {found.type, {found.value[0], found.value[1], found.value[2], found.value[3]}, found.texture};
            return WriteText(name, capacity, Utf8ToUtf16(found.name));
        }
    } // namespace AssetsNatives

    Fenrir::ScriptingExtension Assets::ScriptingExtension()
    {
        return Generated::MakeAssetsExtension(std::make_shared<AssetsScripting>());
    }
} // namespace Fenrir
