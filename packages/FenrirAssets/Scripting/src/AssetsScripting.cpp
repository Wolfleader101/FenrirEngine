#include "FenrirAssetsScripting/AssetsScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssetsScripting/AssetMappings.hpp"
#include "FenrirMath/Colour.hpp"

namespace Fenrir
{
    namespace
    {
        // A warning logged once for each place in this file that notices a call doing nothing, and each reason, in
        // debug builds only.
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            static std::mutex mutex;
            static std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[assets scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        // A handle crosses to and from C# as one number: the generation in the high half, the index in the low (C#'s
        // AssetHandle<T>.Bits).
        template <typename T>
        AssetHandle<T> Unpack(std::uint64_t bits) noexcept
        {
            return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32u)};
        }

        template <typename T>
        std::uint64_t Pack(AssetHandle<T> handle) noexcept
        {
            return (std::uint64_t{handle.generation} << 32u) | handle.index;
        }

        // The app's asset store; warns once for the caller's call site if there is none.
        AssetStore* StoreFor(const CallContext& context, std::string_view function,
                             std::source_location where = std::source_location::current())
        {
            if (AssetStore* store = context.GetApp().TryGetResource<AssetStore>())
            {
                return store;
            }
            WarnOnce(context,
                     std::string(function) + ": the app has no asset store (FenrirAssets is not installed), so it "
                                             "does nothing",
                     where);
            return nullptr;
        }

        // The asset store, if `bits` names a live asset of kind `T` in it; warns once for the caller's call site if
        // not.
        template <typename T>
        AssetStore* Resolve(const CallContext& context, std::uint64_t bits, std::string_view function,
                            std::source_location where = std::source_location::current())
        {
            AssetStore* store = StoreFor(context, function, where);
            if (store == nullptr || store->IsValid(Unpack<T>(bits)))
            {
                return store;
            }
            WarnOnce(context,
                     std::string(function) + ": the " + std::string(Detail::AssetTraits<T>::name) +
                         " handle is empty or stale, so it does nothing",
                     where);
            return nullptr;
        }

        // The owner C#'s AssetOwner names: the active scene or the app. Warns once if it names neither.
        std::optional<AssetOwner> OwnerOf(const CallContext& context, std::uint8_t owner, std::string_view function,
                                          std::source_location where = std::source_location::current())
        {
            if (owner == 0)
            {
                return AssetOwner(context.GetApp().GetActiveScene());
            }
            if (owner == 1)
            {
                return AssetOwner::App();
            }
            WarnOnce(context, std::string(function) + ": no such asset owner, so it does nothing", where);
            return std::nullopt;
        }

        // `count` values from `first` on, or nothing (with a warning) for a negative count or a null pointer with
        // values.
        template <typename T>
        std::optional<std::vector<T>> Copy(const CallContext& context, const T* first, std::int32_t count,
                                           std::string_view function,
                                           std::source_location where = std::source_location::current())
        {
            if (count < 0 || (first == nullptr && count > 0))
            {
                WarnOnce(context, std::string(function) + ": a span is unusable, so it does nothing", where);
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

        // A mesh from C#'s spans, drawn with one material: one submesh over every index.
        std::optional<MeshData> MeshFrom(const CallContext& context, const Math::Vec3* positions,
                                         std::int32_t positionCount, const Math::Vec3* normals,
                                         std::int32_t normalCount, const Math::Vec2* uvs, std::int32_t uvCount,
                                         const std::uint32_t* indices, std::int32_t indexCount,
                                         std::string_view function)
        {
            auto positionValues = Copy(context, positions, positionCount, function);
            auto normalValues = Copy(context, normals, normalCount, function);
            auto uvValues = Copy(context, uvs, uvCount, function);
            auto indexValues = Copy(context, indices, indexCount, function);
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
        std::optional<ImageData> ImageFrom(const CallContext& context, std::int32_t width, std::int32_t height,
                                           const std::uint8_t* rgba8, std::int32_t byteCount, std::string_view function,
                                           std::source_location where = std::source_location::current())
        {
            if (width < 1 || height < 1)
            {
                WarnOnce(context,
                         std::string(function) + ": an image is at least one pixel each way, so it does nothing",
                         where);
                return std::nullopt;
            }
            auto bytes = Copy(context, rgba8, byteCount, function, where);
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
        Result ForKind(const CallContext& context, std::uint8_t kind, std::string_view function, Result otherwise,
                       Action&& action, std::source_location where = std::source_location::current())
        {
            // Not a switch: `kind` comes from C#, so it may be none of the enumerators.
            if (kind == static_cast<std::uint8_t>(Detail::AssetKind::Mesh))
            {
                return action(AssetHandle<Mesh>{});
            }
            if (kind == static_cast<std::uint8_t>(Detail::AssetKind::Image))
            {
                return action(AssetHandle<Image>{});
            }
            if (kind == static_cast<std::uint8_t>(Detail::AssetKind::Material))
            {
                return action(AssetHandle<Material>{});
            }
            if (kind == static_cast<std::uint8_t>(Detail::AssetKind::Shader))
            {
                return action(AssetHandle<Shader>{});
            }
            if (kind == static_cast<std::uint8_t>(Detail::AssetKind::Model))
            {
                return action(AssetHandle<Model>{});
            }
            WarnOnce(context,
                     std::string(function) + ": the handle's type is not Mesh, Image, Material, Shader or Model, so "
                                             "it does nothing",
                     where);
            return otherwise;
        }

        // Writes `value` through `out` if there is one, and says whether there was.
        template <typename V, typename Out>
        bool Write(const std::optional<V>& value, Out* out)
        {
            if (value.has_value() && out != nullptr)
            {
                *out = static_cast<Out>(*value);
                return true;
            }
            return false;
        }

        // The native functions, named apart from the store's own functions.
        namespace Native
        {
            std::uint64_t Load(const CallContext& context, std::uint8_t kind, std::string_view path, std::uint8_t owner)
            {
                AssetStore* store = StoreFor(context, "Assets.Load");
                const std::optional<AssetOwner> assetOwner =
                    store ? OwnerOf(context, owner, "Assets.Load") : std::nullopt;
                if (!assetOwner)
                {
                    return 0;
                }
                // A failed load is logged by the store, with its reason.
                return ForKind(context, kind, "Assets.Load", std::uint64_t{0}, [&]<typename T>(AssetHandle<T>) {
                    return Pack(store->Load<T>(path, *assetOwner).handle);
                });
            }

            bool Unload(const CallContext& context, std::uint8_t kind, std::uint64_t handle)
            {
                return ForKind(context, kind, "Assets.Unload", false, [&]<typename T>(AssetHandle<T>) {
                    AssetStore* store = Resolve<T>(context, handle, "Assets.Unload");
                    return store != nullptr && store->Unload(Unpack<T>(handle));
                });
            }

            bool IsValid(const CallContext& context, std::uint8_t kind, std::uint64_t handle)
            {
                AssetStore* store = StoreFor(context, "Assets.IsValid");
                return store != nullptr &&
                       ForKind(context, kind, "Assets.IsValid", false,
                               [&]<typename T>(AssetHandle<T>) { return store->IsValid(Unpack<T>(handle)); });
            }

            std::uint64_t AddMesh(const CallContext& context, const Math::Vec3* positions, std::int32_t positionCount,
                                  const Math::Vec3* normals, std::int32_t normalCount, const Math::Vec2* uvs,
                                  std::int32_t uvCount, const std::uint32_t* indices, std::int32_t indexCount,
                                  std::uint8_t owner)
            {
                AssetStore* store = StoreFor(context, "Assets.AddMesh");
                const std::optional<AssetOwner> assetOwner =
                    store ? OwnerOf(context, owner, "Assets.AddMesh") : std::nullopt;
                if (!assetOwner)
                {
                    return 0;
                }
                std::optional<MeshData> mesh = MeshFrom(context, positions, positionCount, normals, normalCount, uvs,
                                                        uvCount, indices, indexCount, "Assets.AddMesh");
                return mesh ? Pack(store->Add<Mesh>(std::move(*mesh), *assetOwner)) : 0;
            }

            bool ReplaceMesh(const CallContext& context, std::uint64_t handle, const Math::Vec3* positions,
                             std::int32_t positionCount, const Math::Vec3* normals, std::int32_t normalCount,
                             const Math::Vec2* uvs, std::int32_t uvCount, const std::uint32_t* indices,
                             std::int32_t indexCount)
            {
                AssetStore* store = Resolve<Mesh>(context, handle, "Assets.ReplaceMesh");
                if (store == nullptr)
                {
                    return false;
                }
                std::optional<MeshData> mesh = MeshFrom(context, positions, positionCount, normals, normalCount, uvs,
                                                        uvCount, indices, indexCount, "Assets.ReplaceMesh");
                return mesh && store->Replace(Unpack<Mesh>(handle), std::move(*mesh));
            }

            std::uint64_t AddRenderTarget(const CallContext& context, std::int32_t width, std::int32_t height,
                                          std::uint8_t owner)
            {
                AssetStore* store = StoreFor(context, "Assets.AddRenderTarget");
                const std::optional<AssetOwner> assetOwner =
                    store ? OwnerOf(context, owner, "Assets.AddRenderTarget") : std::nullopt;
                if (!assetOwner)
                {
                    return 0;
                }
                std::optional<ImageData> image =
                    ImageFrom(context, width, height, nullptr, 0, "Assets.AddRenderTarget");
                return image ? Pack(store->Add<Image>(std::move(*image), *assetOwner)) : 0;
            }

            bool ResizeRenderTarget(const CallContext& context, std::uint64_t handle, std::int32_t width,
                                    std::int32_t height)
            {
                AssetStore* store = Resolve<Image>(context, handle, "Assets.ResizeRenderTarget");
                if (store == nullptr)
                {
                    return false;
                }
                if (!store->TryGet(Unpack<Image>(handle))->pixels.empty())
                {
                    WarnOnce(context, "Assets.ResizeRenderTarget: the image has pixels, so it is not a render target "
                                      "and it does nothing");
                    return false;
                }
                std::optional<ImageData> image =
                    ImageFrom(context, width, height, nullptr, 0, "Assets.ResizeRenderTarget");
                return image && store->Replace(Unpack<Image>(handle), std::move(*image));
            }

            std::uint64_t AddImage(const CallContext& context, std::int32_t width, std::int32_t height,
                                   const std::uint8_t* rgba8, std::int32_t byteCount, std::uint8_t owner)
            {
                AssetStore* store = StoreFor(context, "Assets.AddImage");
                const std::optional<AssetOwner> assetOwner =
                    store ? OwnerOf(context, owner, "Assets.AddImage") : std::nullopt;
                if (!assetOwner)
                {
                    return 0;
                }
                std::optional<ImageData> image = ImageFrom(context, width, height, rgba8, byteCount, "Assets.AddImage");
                return image ? Pack(store->Add<Image>(std::move(*image), *assetOwner)) : 0;
            }

            bool ReplaceImage(const CallContext& context, std::uint64_t handle, std::int32_t width, std::int32_t height,
                              const std::uint8_t* rgba8, std::int32_t byteCount)
            {
                AssetStore* store = Resolve<Image>(context, handle, "Assets.ReplaceImage");
                if (store == nullptr)
                {
                    return false;
                }
                std::optional<ImageData> image =
                    ImageFrom(context, width, height, rgba8, byteCount, "Assets.ReplaceImage");
                return image && store->Replace(Unpack<Image>(handle), std::move(*image));
            }

            std::uint64_t CreateMaterial(const CallContext& context, std::uint64_t shader, std::uint8_t owner)
            {
                AssetStore* store = Resolve<Shader>(context, shader, "Materials.Create");
                const std::optional<AssetOwner> assetOwner =
                    store ? OwnerOf(context, owner, "Materials.Create") : std::nullopt;
                return assetOwner ? Pack(store->CreateMaterial(Unpack<Shader>(shader), *assetOwner)) : 0;
            }

            std::uint64_t CloneMaterial(const CallContext& context, std::uint64_t material, std::uint8_t owner)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.Clone");
                const std::optional<AssetOwner> assetOwner =
                    store ? OwnerOf(context, owner, "Materials.Clone") : std::nullopt;
                return assetOwner ? Pack(store->CloneMaterial(Unpack<Material>(material), *assetOwner)) : 0;
            }

            bool SetFloat(const CallContext& context, std::uint64_t material, std::string_view name, float value)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetFloat");
                return store != nullptr && store->SetFloat(Unpack<Material>(material), name, value);
            }

            bool SetVector2(const CallContext& context, std::uint64_t material, std::string_view name,
                            const Math::Vec2* value)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetVector2");
                return store != nullptr && value != nullptr &&
                       store->SetVector2(Unpack<Material>(material), name, *value);
            }

            bool SetVector3(const CallContext& context, std::uint64_t material, std::string_view name,
                            const Math::Vec3* value)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetVector3");
                return store != nullptr && value != nullptr &&
                       store->SetVector3(Unpack<Material>(material), name, *value);
            }

            bool SetVector4(const CallContext& context, std::uint64_t material, std::string_view name,
                            const Math::Vec4* value)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetVector4");
                return store != nullptr && value != nullptr &&
                       store->SetVector4(Unpack<Material>(material), name, *value);
            }

            bool SetColour(const CallContext& context, std::uint64_t material, std::string_view name,
                           const Colour* value)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetColour");
                return store != nullptr && value != nullptr &&
                       store->SetColour(Unpack<Material>(material), name, *value);
            }

            bool SetTexture(const CallContext& context, std::uint64_t material, std::string_view name,
                            std::uint64_t image, const SamplerDescription* sampler)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetTexture");
                return store != nullptr && sampler != nullptr &&
                       store->SetTexture(Unpack<Material>(material), name, Unpack<Image>(image), *sampler);
            }

            bool SetBlendMode(const CallContext& context, std::uint64_t material, std::uint8_t blendMode)
            {
                if (blendMode > static_cast<std::uint8_t>(BlendMode::Additive))
                {
                    WarnOnce(context, "Materials.SetBlendMode: no such blend mode, so it does nothing");
                    return false;
                }
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetBlendMode");
                return store != nullptr &&
                       store->SetBlendMode(Unpack<Material>(material), static_cast<BlendMode>(blendMode));
            }

            bool SetAlphaCutoff(const CallContext& context, std::uint64_t material, float alphaCutoff)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetAlphaCutoff");
                return store != nullptr && store->SetAlphaCutoff(Unpack<Material>(material), alphaCutoff);
            }

            bool SetDoubleSided(const CallContext& context, std::uint64_t material, bool doubleSided)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.SetDoubleSided");
                return store != nullptr && store->SetDoubleSided(Unpack<Material>(material), doubleSided);
            }

            // One read native function: writes what `read` gives for a live material to `out`, if it gives anything.
            // C# zeroes `out` before the call, so a read that finds nothing leaves it zero (not, say, a Colour's
            // white). Warns once per `function` for a stale material.
            template <typename Out, typename Read>
            bool ReadInto(const CallContext& context, std::uint64_t material, Out* out, std::string_view function,
                          Read&& read, std::source_location where = std::source_location::current())
            {
                AssetStore* store = Resolve<Material>(context, material, function, where);
                return store != nullptr && Write(read(*store, Unpack<Material>(material)), out);
            }

// Each pair of TryGet and TryGetOverride natives for one value type: `Type` is the C++ type C# writes into, `Name` the
// store function's suffix.
#define FENRIR_MATERIAL_READS(Name, Type)                                                                           \
    bool TryGet##Name(const CallContext& context, std::uint64_t material, std::string_view name, Type* out)         \
    {                                                                                                               \
        return ReadInto(context, material, out, "Materials.TryGet" #Name,                                           \
                        [name](const AssetStore& store, AssetHandle<Material> handle) {                             \
                            return store.TryGet##Name(handle, name);                                                \
                        });                                                                                         \
    }                                                                                                               \
    bool TryGetOverride##Name(const CallContext& context, std::uint64_t material, std::string_view name, Type* out) \
    {                                                                                                               \
        return ReadInto(context, material, out, "Materials.TryGetOverride" #Name,                                   \
                        [name](const AssetStore& store, AssetHandle<Material> handle) {                             \
                            return store.TryGetOverride##Name(handle, name);                                        \
                        });                                                                                         \
    }

            FENRIR_MATERIAL_READS(Float, float)
            FENRIR_MATERIAL_READS(Vector2, Math::Vec2)
            FENRIR_MATERIAL_READS(Vector3, Math::Vec3)
            FENRIR_MATERIAL_READS(Vector4, Math::Vec4)
            FENRIR_MATERIAL_READS(Colour, Colour)
            FENRIR_MATERIAL_READS(Texture, MaterialTexture)
#undef FENRIR_MATERIAL_READS

            bool IsSet(const CallContext& context, std::uint64_t material, std::string_view name)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.IsSet");
                return store != nullptr && store->IsSet(Unpack<Material>(material), name);
            }

            bool Reset(const CallContext& context, std::uint64_t material, std::string_view name)
            {
                AssetStore* store = Resolve<Material>(context, material, "Materials.Reset");
                return store != nullptr && store->Reset(Unpack<Material>(material), name);
            }

            std::int32_t GetParameterCount(const CallContext& context, std::uint64_t shader)
            {
                AssetStore* store = Resolve<Shader>(context, shader, "Shaders.GetParameterCount");
                return store != nullptr
                           ? static_cast<std::int32_t>(store->GetShaderParameters(Unpack<Shader>(shader)).size())
                           : 0;
            }

            std::int32_t GetParameter(const CallContext& context, std::uint64_t shader, std::int32_t index,
                                      ScriptShaderParameter* parameter, char16_t* name, std::int32_t capacity)
            {
                if (parameter != nullptr)
                {
                    *parameter = ScriptShaderParameter{};
                }
                AssetStore* store = Resolve<Shader>(context, shader, "Shaders.GetParameter");
                if (store == nullptr)
                {
                    return 0;
                }
                const std::span<const ShaderParameter> parameters = store->GetShaderParameters(Unpack<Shader>(shader));
                if (index < 0 || static_cast<std::size_t>(index) >= parameters.size())
                {
                    WarnOnce(context, "Shaders.GetParameter: the shader has no parameter at that index, so it does "
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
        } // namespace Native
    } // namespace

    Fenrir::ScriptingExtension Assets::ScriptingExtension()
    {
        const std::string assets = "Fenrir.Assets.AssetsNative.";
        const std::string materials = "Fenrir.Assets.MaterialsNative.";
        const std::string shaders = "Fenrir.Assets.ShadersNative.";
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirAssets";
        extension.sharedAssemblies = {"Fenrir.Assets.dll"};
        extension.functions = {
            NativeFunction::Make<&Native::Load>(assets + "Load"),
            NativeFunction::Make<&Native::Unload>(assets + "Unload"),
            NativeFunction::Make<&Native::IsValid>(assets + "IsValid"),
            NativeFunction::Make<&Native::AddMesh>(assets + "AddMesh"),
            NativeFunction::Make<&Native::ReplaceMesh>(assets + "ReplaceMesh"),
            NativeFunction::Make<&Native::AddRenderTarget>(assets + "AddRenderTarget"),
            NativeFunction::Make<&Native::ResizeRenderTarget>(assets + "ResizeRenderTarget"),
            NativeFunction::Make<&Native::AddImage>(assets + "AddImage"),
            NativeFunction::Make<&Native::ReplaceImage>(assets + "ReplaceImage"),

            NativeFunction::Make<&Native::CreateMaterial>(materials + "Create"),
            NativeFunction::Make<&Native::CloneMaterial>(materials + "Clone"),
            NativeFunction::Make<&Native::SetFloat>(materials + "SetFloat"),
            NativeFunction::Make<&Native::SetVector2>(materials + "SetVector2"),
            NativeFunction::Make<&Native::SetVector3>(materials + "SetVector3"),
            NativeFunction::Make<&Native::SetVector4>(materials + "SetVector4"),
            NativeFunction::Make<&Native::SetColour>(materials + "SetColour"),
            NativeFunction::Make<&Native::SetTexture>(materials + "SetTexture"),
            NativeFunction::Make<&Native::SetBlendMode>(materials + "SetBlendMode"),
            NativeFunction::Make<&Native::SetAlphaCutoff>(materials + "SetAlphaCutoff"),
            NativeFunction::Make<&Native::SetDoubleSided>(materials + "SetDoubleSided"),
            NativeFunction::Make<&Native::TryGetFloat>(materials + "TryGetFloat"),
            NativeFunction::Make<&Native::TryGetVector2>(materials + "TryGetVector2"),
            NativeFunction::Make<&Native::TryGetVector3>(materials + "TryGetVector3"),
            NativeFunction::Make<&Native::TryGetVector4>(materials + "TryGetVector4"),
            NativeFunction::Make<&Native::TryGetColour>(materials + "TryGetColour"),
            NativeFunction::Make<&Native::TryGetTexture>(materials + "TryGetTexture"),
            NativeFunction::Make<&Native::TryGetOverrideFloat>(materials + "TryGetOverrideFloat"),
            NativeFunction::Make<&Native::TryGetOverrideVector2>(materials + "TryGetOverrideVector2"),
            NativeFunction::Make<&Native::TryGetOverrideVector3>(materials + "TryGetOverrideVector3"),
            NativeFunction::Make<&Native::TryGetOverrideVector4>(materials + "TryGetOverrideVector4"),
            NativeFunction::Make<&Native::TryGetOverrideColour>(materials + "TryGetOverrideColour"),
            NativeFunction::Make<&Native::TryGetOverrideTexture>(materials + "TryGetOverrideTexture"),
            NativeFunction::Make<&Native::IsSet>(materials + "IsSet"),
            NativeFunction::Make<&Native::Reset>(materials + "Reset"),

            NativeFunction::Make<&Native::GetParameterCount>(shaders + "GetParameterCount"),
            NativeFunction::Make<&Native::GetParameter>(shaders + "GetParameter"),
        };
        return extension;
    }
} // namespace Fenrir
