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

        // Writes the handle the store made to `out` and says so; false, leaving `out` the empty handle the
        // registration zeroed it to (ADR-0045), when the store refused it (the empty handle).
        template <typename T>
        bool Give(AssetHandle<T> made, AssetHandle<T>* out)
        {
            if (made.IsEmpty())
            {
                return false;
            }
            *out = made;
            return true;
        }

        // One material read: writes what `read` gives for a live material to `out`, if it gives anything, and says
        // whether it did. Warns once per call site for a stale material; `out` holds its default until written (the
        // registration zeroes it, ADR-0045).
        template <typename Out, typename Read>
        bool ReadInto(const NativeGuard& guard, const AssetHandle<Material>& material, Out* out, Read&& read,
                      std::source_location where = std::source_location::current())
        {
            AssetStore* store = Resolve<Material>(guard, material, where);
            if (store == nullptr)
            {
                return false;
            }
            const std::optional<Out> value = read(*store, material);
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
    // (Fenrir.Assets.AssetsNative), their registration and, from their @facade marks, the generated parts of the three
    // facades from that. A handle crosses as the bound type it is: taken as `const AssetHandle<T>*` (C#'s `in
    // AssetHandle<T>`), given through an `AssetHandle<T>*` output (ADR-0044); the three kind-generic natives take the
    // kind-neutral ScriptAssetHandle beside FenrirAssets's Detail::AssetKind, since no one kind is theirs. Text crosses
    // as a view; spans as a pointer and a count. Each returns doing nothing (false or zero) with no asset store, a
    // stale or empty handle, or an unusable argument, warning once per place under its own name through its guard; its
    // outputs hold their defaults because the registration zeroed them (ADR-0045), so a native that gives a handle and
    // fails leaves the empty one. Not in an anonymous namespace: the generated natives header declares them.
    namespace AssetsNatives
    {
        /**
         * @brief Loads the asset of `kind` at `path` for `owner` (AssetStore::Load).
         * @param[out] handle The asset's handle, of no stated kind (C#'s AssetHandle<T>.FromNative); the empty handle
         * if the load failed (the store logs why).
         * @return Whether it loaded.
         */
        bool Load(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind, std::string_view path,
                  ScriptAssetOwner owner, ScriptAssetHandle* handle)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return false;
            }
            return ForKind(guard, kind, false, [&]<typename T>(AssetHandle<T>) {
                const AssetHandle<T> loaded = placed->store.Load<T>(path, placed->owner).handle;
                if (loaded.IsEmpty())
                {
                    return false;
                }
                *handle = {loaded.index, loaded.generation};
                return true;
            });
        }

        /**
         * @brief Frees the asset of `kind` now, whoever owns it.
         * @param handle The asset's handle, of no stated kind (C#'s AssetHandle<T>.ToNative).
         * @return Whether the handle named a live asset.
         */
        bool Unload(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind,
                    const ScriptAssetHandle* handle)
        {
            const NativeGuard guard(context, assets.warnings);
            return ForKind(guard, kind, false, [&]<typename T>(AssetHandle<T>) {
                const AssetHandle<T> typed{handle->index, handle->generation};
                AssetStore* store = Resolve<T>(guard, typed);
                return store != nullptr && store->Unload(typed);
            });
        }

        /**
         * @brief Whether `handle` names a live asset of `kind`.
         * @param handle The asset's handle, of no stated kind (C#'s AssetHandle<T>.ToNative).
         */
        bool IsValid(const CallContext& context, AssetsScripting& assets, Detail::AssetKind kind,
                     const ScriptAssetHandle* handle)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = StoreFor(guard);
            return store != nullptr && ForKind(guard, kind, false, [&]<typename T>(AssetHandle<T>) {
                       return store->IsValid(AssetHandle<T>{handle->index, handle->generation});
                   });
        }

        /**
         * @brief Adds a mesh made of these vertex streams and triangles (three indices each, counter-clockwise seen
         * from the front) for `owner`, drawn with one material. `normals` and `uvs` may be empty; otherwise each holds
         * one value per position. The data is copied, and the mesh's bounds are computed from the positions.
         * @param[out] mesh The mesh; the empty handle (and an error logged) if the streams or indices break those
         * rules.
         * @return Whether the store accepted it.
         * @facade Assets.AddMesh returns mesh
         */
        bool AddMesh(const CallContext& context, AssetsScripting& assets, const Math::Vec3* positions,
                     std::int32_t positionCount, const Math::Vec3* normals, std::int32_t normalCount,
                     const Math::Vec2* uvs, std::int32_t uvCount, const std::uint32_t* indices, std::int32_t indexCount,
                     ScriptAssetOwner owner, AssetHandle<Mesh>* mesh)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return false;
            }
            return Give(placed->store.Add<Mesh>(
                            MeshFrom(positions, positionCount, normals, normalCount, uvs, uvCount, indices, indexCount),
                            placed->owner),
                        mesh);
        }

        /**
         * @brief Swaps `mesh`'s data for these streams and triangles, as AddMesh takes them, and bumps its revision so
         * every user sees the change. Its bounds are computed again.
         * @return Whether it was replaced: false for a stale handle or data that breaks the rules, which leave it
         * unchanged.
         * @facade Assets.ReplaceMesh
         */
        bool ReplaceMesh(const CallContext& context, AssetsScripting& assets, const AssetHandle<Mesh>* mesh,
                         const Math::Vec3* positions, std::int32_t positionCount, const Math::Vec3* normals,
                         std::int32_t normalCount, const Math::Vec2* uvs, std::int32_t uvCount,
                         const std::uint32_t* indices, std::int32_t indexCount)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Mesh>(guard, *mesh);
            return store != nullptr && store->Replace(*mesh, MeshFrom(positions, positionCount, normals, normalCount,
                                                                      uvs, uvCount, indices, indexCount));
        }

        /**
         * @brief Adds a render target for `owner`: an RGBA8 sRGB image of this size with no pixels, which a camera
         * draws into and materials sample (ADR-0026).
         * @param[out] image The image; the empty handle if either side is under one pixel.
         * @return Whether the store accepted it.
         * @facade Assets.AddRenderTarget returns image
         */
        bool AddRenderTarget(const CallContext& context, AssetsScripting& assets, std::int32_t width,
                             std::int32_t height, ScriptAssetOwner owner, AssetHandle<Image>* image)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return false;
            }
            std::optional<ImageData> data = ImageFrom(guard, width, height, nullptr, 0);
            return data && Give(placed->store.Add<Image>(std::move(*data), placed->owner), image);
        }

        /**
         * @brief Gives the render target `image` a new size, bumping its revision.
         * @param[in] image One handle, not a span of them, though `width` follows it.
         * @return Whether it was resized: false for a stale handle, an image with pixels (which is not a render
         * target), or a side under one pixel.
         * @facade Assets.ResizeRenderTarget
         */
        bool ResizeRenderTarget(const CallContext& context, AssetsScripting& assets, const AssetHandle<Image>* image,
                                std::int32_t width, std::int32_t height)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Image>(guard, *image);
            if (store == nullptr)
            {
                return false;
            }
            if (!store->TryGet(*image)->pixels.empty())
            {
                guard.Warn("the image has pixels, so it is not a render target and it does nothing");
                return false;
            }
            std::optional<ImageData> data = ImageFrom(guard, width, height, nullptr, 0);
            return data && store->Replace(*image, std::move(*data));
        }

        /**
         * @brief Adds an RGBA8 sRGB image of this size from `rgba8` for `owner`: four bytes per pixel (red, green,
         * blue, alpha), rows top to bottom. The pixels are copied.
         * @param[out] image The image; the empty handle (and an error logged) if the pixels are not `width * height *
         * 4` bytes.
         * @return Whether the store accepted it.
         * @facade Assets.AddImage returns image
         */
        bool AddImage(const CallContext& context, AssetsScripting& assets, std::int32_t width, std::int32_t height,
                      const std::uint8_t* rgba8, std::int32_t byteCount, ScriptAssetOwner owner,
                      AssetHandle<Image>* image)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed = assets.StoreAndOwnerFor(guard, owner);
            if (!placed)
            {
                return false;
            }
            std::optional<ImageData> data = ImageFrom(guard, width, height, rgba8, byteCount);
            return data && Give(placed->store.Add<Image>(std::move(*data), placed->owner), image);
        }

        /**
         * @brief Swaps `image`'s data for an RGBA8 sRGB image of this size from `rgba8`, as AddImage takes it, and
         * bumps its revision.
         * @param[in] image One handle, not a span of them, though `width` follows it.
         * @return Whether it was replaced: false for a stale handle or the wrong number of bytes, which leave it
         * unchanged.
         * @facade Assets.ReplaceImage
         */
        bool ReplaceImage(const CallContext& context, AssetsScripting& assets, const AssetHandle<Image>* image,
                          std::int32_t width, std::int32_t height, const std::uint8_t* rgba8, std::int32_t byteCount)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Image>(guard, *image);
            if (store == nullptr)
            {
                return false;
            }
            std::optional<ImageData> data = ImageFrom(guard, width, height, rgba8, byteCount);
            return data && store->Replace(*image, std::move(*data));
        }

        /**
         * @brief A new material drawing with `shader`, for `owner` (AssetStore::CreateMaterial): opaque, single-sided,
         * with no overrides.
         * @param[out] material The material; the empty handle if the shader handle does not resolve.
         * @return Whether it was made.
         * @facade Materials.Create returns material
         */
        bool CreateMaterial(const CallContext& context, AssetsScripting& assets, const AssetHandle<Shader>* shader,
                            ScriptAssetOwner owner, AssetHandle<Material>* material)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed =
                assets.StoreAndOwnerFor<Shader>(guard, *shader, owner);
            return placed && Give(placed->store.CreateMaterial(*shader, placed->owner), material);
        }

        /**
         * @brief A new material for `owner` copying `material`'s shader, blend mode, alpha cutoff, double-sidedness and
         * parameter overrides (AssetStore::CloneMaterial). Only overrides are copied, so the clone's unset parameters
         * still follow the shader's defaults.
         * @param[out] clone The clone; the empty handle if `material` does not resolve.
         * @return Whether it was made.
         * @facade Materials.Clone returns clone
         */
        bool CloneMaterial(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                           ScriptAssetOwner owner, AssetHandle<Material>* clone)
        {
            const NativeGuard guard(context, assets.warnings);
            const std::optional<AssetsScripting::StoreAndOwner> placed =
                assets.StoreAndOwnerFor<Material>(guard, *material, owner);
            return placed && Give(placed->store.CloneMaterial(*material, placed->owner), clone);
        }

        /**
         * @brief Sets a `float` parameter (AssetStore::SetFloat).
         * @facade Materials.SetFloat
         */
        bool SetFloat(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                      std::string_view name, float value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetFloat(*material, name, value);
        }

        /**
         * @brief Sets a `vec2` parameter (AssetStore::SetVector2).
         * @facade Materials.SetVector2
         */
        bool SetVector2(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                        std::string_view name, const Math::Vec2* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetVector2(*material, name, *value);
        }

        /**
         * @brief Sets a `vec3` parameter (AssetStore::SetVector3).
         * @facade Materials.SetVector3
         */
        bool SetVector3(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                        std::string_view name, const Math::Vec3* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetVector3(*material, name, *value);
        }

        /**
         * @brief Sets a `vec4` parameter (AssetStore::SetVector4).
         * @facade Materials.SetVector4
         */
        bool SetVector4(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                        std::string_view name, const Math::Vec4* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetVector4(*material, name, *value);
        }

        /**
         * @brief Sets a `vec4` parameter to a colour's linear red, green, blue and alpha (AssetStore::SetColour).
         * @facade Materials.SetColour
         */
        bool SetColour(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                       std::string_view name, const Colour* value)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetColour(*material, name, *value);
        }

        /**
         * @brief Sets a texture parameter to `image`, read with `sampler` (AssetStore::SetTexture).
         * @facade Materials.SetTexture
         */
        bool SetTexture(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                        std::string_view name, const AssetHandle<Image>* image, const SamplerDescription* sampler)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetTexture(*material, name, *image, *sampler);
        }

        /**
         * @brief Sets how the material's pixels combine with what is behind them (AssetStore::SetBlendMode).
         * @facade Materials.SetBlendMode
         */
        bool SetBlendMode(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                          BlendMode blendMode)
        {
            const NativeGuard guard(context, assets.warnings);
            if (!IsEnumerator<BlendMode::Additive>(blendMode))
            {
                guard.Warn("no such blend mode, so it does nothing");
                return false;
            }
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetBlendMode(*material, blendMode);
        }

        /**
         * @brief Sets the alpha below which a masked material discards a pixel (AssetStore::SetAlphaCutoff).
         * @facade Materials.SetAlphaCutoff
         */
        bool SetAlphaCutoff(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                            float alphaCutoff)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetAlphaCutoff(*material, alphaCutoff);
        }

        /**
         * @brief Sets whether both faces of the material are drawn (AssetStore::SetDoubleSided).
         * @facade Materials.SetDoubleSided
         */
        bool SetDoubleSided(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                            bool doubleSided)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->SetDoubleSided(*material, doubleSided);
        }

        /**
         * @brief Reads a `float` parameter's effective value: the material's override, else the shader's parameter
         * default (AssetStore::TryGetFloat).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one: false only if the material does not resolve, its shader lacks the name, or
         * the parameter is another type.
         * @facade Materials.TryGetFloat
         */
        bool TryGetFloat(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                         std::string_view name, float* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetFloat(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec2` parameter's effective value (AssetStore::TryGetVector2).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetVector2
         */
        bool TryGetVector2(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                           std::string_view name, Math::Vec2* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetVector2(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec3` parameter's effective value (AssetStore::TryGetVector3).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetVector3
         */
        bool TryGetVector3(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                           std::string_view name, Math::Vec3* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetVector3(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec4` parameter's effective value (AssetStore::TryGetVector4).
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetVector4
         */
        bool TryGetVector4(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                           std::string_view name, Math::Vec4* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetVector4(handle, name);
                            });
        }

        /**
         * @brief Reads a `vec4` parameter's effective value as a colour (AssetStore::TryGetColour).
         * @param[out] value The value; the default (white) when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetColour
         */
        bool TryGetColour(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                          std::string_view name, Colour* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetColour(handle, name);
                            });
        }

        /**
         * @brief Reads a texture parameter's effective value (AssetStore::TryGetTexture). Left unset, it reads the
         * empty image (the shader's default texture) with the default sampler.
         * @param[out] value The value; the default when there is none.
         * @return Whether there was one: false only if the material does not resolve, its shader lacks the name, or
         * the parameter is another type.
         * @facade Materials.TryGetTexture
         */
        bool TryGetTexture(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                           std::string_view name, MaterialTexture* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetTexture(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `float` override, if it sets one of this type
         * (AssetStore::TryGetOverrideFloat). It does not consult the shader, so it also reads an override the shader
         * no longer has.
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetOverrideFloat
         */
        bool TryGetOverrideFloat(const CallContext& context, AssetsScripting& assets,
                                 const AssetHandle<Material>* material, std::string_view name, float* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideFloat(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec2` override (AssetStore::TryGetOverrideVector2).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetOverrideVector2
         */
        bool TryGetOverrideVector2(const CallContext& context, AssetsScripting& assets,
                                   const AssetHandle<Material>* material, std::string_view name, Math::Vec2* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideVector2(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec3` override (AssetStore::TryGetOverrideVector3).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetOverrideVector3
         */
        bool TryGetOverrideVector3(const CallContext& context, AssetsScripting& assets,
                                   const AssetHandle<Material>* material, std::string_view name, Math::Vec3* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideVector3(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec4` override (AssetStore::TryGetOverrideVector4).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetOverrideVector4
         */
        bool TryGetOverrideVector4(const CallContext& context, AssetsScripting& assets,
                                   const AssetHandle<Material>* material, std::string_view name, Math::Vec4* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideVector4(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own `vec4` override as a colour (AssetStore::TryGetOverrideColour).
         * @param[out] value The override; the default (white) when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetOverrideColour
         */
        bool TryGetOverrideColour(const CallContext& context, AssetsScripting& assets,
                                  const AssetHandle<Material>* material, std::string_view name, Colour* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideColour(handle, name);
                            });
        }

        /**
         * @brief Reads the material's own texture override (AssetStore::TryGetOverrideTexture).
         * @param[out] value The override; the default when there is none.
         * @return Whether there was one.
         * @facade Materials.TryGetOverrideTexture
         */
        bool TryGetOverrideTexture(const CallContext& context, AssetsScripting& assets,
                                   const AssetHandle<Material>* material, std::string_view name, MaterialTexture* value)
        {
            return ReadInto(NativeGuard(context, assets.warnings), *material, value,
                            [name](const AssetStore& store, AssetHandle<Material> handle) {
                                return store.TryGetOverrideTexture(handle, name);
                            });
        }

        /**
         * @brief Whether the material has an override of the parameter (AssetStore::IsSet).
         * @facade Materials.IsSet
         */
        bool IsSet(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                   std::string_view name)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->IsSet(*material, name);
        }

        /**
         * @brief Removes the material's override of the parameter, so it reads the shader's default again
         * (AssetStore::Reset).
         * @return True, and the revision bumped, only if there was an override to remove.
         * @facade Materials.Reset
         */
        bool Reset(const CallContext& context, AssetsScripting& assets, const AssetHandle<Material>* material,
                   std::string_view name)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Material>(guard, *material);
            return store != nullptr && store->Reset(*material, name);
        }

        /**
         * @brief How many material parameters `shader` has (AssetStore::GetShaderParameters).
         * @facade Shaders.GetParameterCount
         */
        std::int32_t GetShaderParameterCount(const CallContext& context, AssetsScripting& assets,
                                             const AssetHandle<Shader>* shader)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Shader>(guard, *shader);
            return store != nullptr ? static_cast<std::int32_t>(store->GetShaderParameters(*shader).size()) : 0;
        }

        /**
         * @brief Reads parameter `index` of `shader`: its type and default into `parameter`, and its name into `name`,
         * as much as fits.
         * @param[in] shader One handle, not a span of them, though `index` follows it.
         * @param[out] parameter The parameter without its name; the default when there is none.
         * @return The name's whole length in chars, so a result larger than the span says how large a span would hold
         * it; zero, with `parameter` zeroed, if there is no such parameter.
         * @facade Shaders.GetParameter
         */
        std::int32_t GetShaderParameter(const CallContext& context, AssetsScripting& assets,
                                        const AssetHandle<Shader>* shader, std::int32_t index,
                                        ScriptShaderParameter* parameter, char16_t* name, std::int32_t capacity)
        {
            const NativeGuard guard(context, assets.warnings);
            AssetStore* store = Resolve<Shader>(guard, *shader);
            if (store == nullptr)
            {
                return 0;
            }
            const std::span<const ShaderParameter> parameters = store->GetShaderParameters(*shader);
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
