#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class ILogger;
    class Scene;

    namespace Detail
    {
        /** @brief What the store set up. Defined in FenrirAssets's private headers; nothing else can use it. */
        struct AssetStoreState;

        /** @brief An asset handle without its type: what the store's untyped core works with. */
        struct AssetSlot
        {
            std::uint32_t index = 0;
            std::uint32_t generation = 0;
        };

        /** @brief One asset's data on the heap, destroyed through the deleter chosen where its type was known. */
        using AssetBox = std::unique_ptr<void, void (*)(void*)>;

        template <typename D>
        void DestroyAssetData(void* data)
        {
            delete static_cast<D*>(data);
        }

        /**
         * @name Checks before the store takes data
         * Each checks one kind of data and completes what the store computes (a mesh's bounds). It returns an empty
         * string when the data is usable, else why it is not.
         * @{
         */
        [[nodiscard]] std::string PrepareAsset(MeshData& mesh);
        [[nodiscard]] std::string PrepareAsset(ImageData& image);
        [[nodiscard]] std::string PrepareAsset(ShaderData& shader);
        [[nodiscard]] std::string PrepareAsset(MaterialData& material);
        /** @} */
    } // namespace Detail

    /** @brief How Assets::Install sets the asset store up. */
    struct AssetsConfig
    {
        /**
         * @brief The asset root: the directory relative asset paths resolve against. Empty means `assets/` beside the
         * executable. A relative root is taken relative to the working directory when the store is made.
         */
        std::filesystem::path root;
    };

    /**
     * @brief Who an asset is loaded or added for: the app, or a scene. An asset lives until its last owner ends (the
     * app's assets end with the asset store, a scene's when the scene is destroyed) or until it is unloaded. Owners
     * are not counted: naming the same owner twice is the same as naming it once.
     */
    class AssetOwner
    {
      public:
        /** @brief The app: the asset lives as long as the asset store, unless it is unloaded. */
        [[nodiscard]] static AssetOwner App() noexcept
        {
            return {};
        }

        /** @brief `scene`: the asset is released when the scene is destroyed. */
        AssetOwner(Scene& scene) noexcept : m_scene(&scene)
        {
        }

        /** @brief The scene this owner names, or null for the app. */
        [[nodiscard]] Scene* GetScene() const noexcept
        {
            return m_scene;
        }

      private:
        AssetOwner() = default;

        Scene* m_scene = nullptr;
    };

    /** @brief Why a load failed. */
    enum class LoadFailure : std::uint8_t
    {
        /** @brief No file at the resolved path. */
        FileNotFound,
        /** @brief The file is there but could not be read or parsed; the importer's message says why. */
        ImportFailed,
        /** @brief The file has no sub-asset with the label after its `#`. */
        LabelNotFound,
        /** @brief Nothing imports this kind of asset from a file yet. */
        NoImporter,
    };

    /** @brief Why a load failed, for a log or an editor. */
    struct LoadDiagnostic
    {
        /** @brief The path that was tried: the file resolved against the asset root, with its `#Label` if it had one.
         */
        std::string path;
        /** @brief Why it failed. */
        LoadFailure reason = LoadFailure::FileNotFound;
        /** @brief The importer's own message, or a description of the reason when no importer ran. */
        std::string importerMessage;
    };

    /** @brief What AssetStore::Load gives back: the handle, or the diagnostic saying why there is none. */
    template <typename T>
    struct LoadResult
    {
        /** @brief The loaded asset; empty when the load failed. */
        AssetHandle<T> handle;
        /** @brief Why the load failed; empty when it succeeded. */
        std::optional<LoadDiagnostic> diagnostic;

        /** @brief Whether the load succeeded. */
        [[nodiscard]] bool Ok() const noexcept
        {
            return !diagnostic.has_value();
        }
    };

    /**
     * @brief The asset store: every asset the app holds, as plain CPU-side data behind asset handles (ADR-0021). An app
     * resource, made by Assets::Install and reached with `app.GetResource<AssetStore>()`.
     *
     * Main thread only: loading, adding, replacing, unloading and material edits must not run alongside anything else
     * that uses the store. Reads (TryGet, IsValid, Revision, the material TryGet calls) may run together from any
     * thread while nothing writes.
     */
    class AssetStore
    {
      public:
        /** @brief A store whose messages go to `logger`, which must outlive it. Assets::Install makes it. */
        AssetStore(ILogger& logger, AssetsConfig config);
        ~AssetStore();
        AssetStore(const AssetStore&) = delete;
        AssetStore& operator=(const AssetStore&) = delete;
        AssetStore(AssetStore&&) = delete;
        AssetStore& operator=(AssetStore&&) = delete;

        /**
         * @brief Loads the asset at `path` for `owner`, blocking until it is read. A relative path resolves against
         * the asset root, an absolute one is taken as given, and `file#Label` names a sub-asset inside the file.
         *
         * A path already loaded as this kind of asset is not read again: it gives back the same handle and adds
         * `owner` to the asset's owners. A failure is logged and gives a diagnostic instead of a handle. Images are
         * read from PNG, JPEG (both RGBA8, sRGB) and HDR (RGBA32F, linear).
         */
        template <typename T>
        [[nodiscard]] LoadResult<T> Load(std::string_view path, AssetOwner owner)
        {
            LoadResult<T> result;
            const Detail::AssetSlot slot = LoadSlot(Detail::AssetTraits<T>::kind, path, owner, result.diagnostic);
            result.handle = {slot.index, slot.generation};
            return result;
        }

        /**
         * @brief Adds `data` as a new asset owned by `owner`. It has no path, so nothing else ever shares it. Data
         * that breaks its type's rules is refused with a logged error, and the handle is empty. A mesh's bounds are
         * computed here.
         */
        template <typename T>
        [[nodiscard]] AssetHandle<T> Add(AssetData<T> data, AssetOwner owner)
        {
            using Data = AssetData<T>;
            if (const std::string problem = Detail::PrepareAsset(data); !problem.empty())
            {
                Refuse(Detail::AssetTraits<T>::name, "add", problem);
                return {};
            }
            Detail::AssetBox box(new Data(std::move(data)), &Detail::DestroyAssetData<Data>);
            const Detail::AssetSlot slot = AddSlot(Detail::AssetTraits<T>::kind, std::move(box), owner);
            return {slot.index, slot.generation};
        }

        /**
         * @brief Swaps the data of the asset `handle` names for `data` and bumps its revision, so every user sees the
         * change. Its owners, path and handle stay. False, with a logged error, if the handle does not resolve or the
         * data breaks its type's rules; the asset is then unchanged. A mesh's bounds are computed here. Replacing a
         * shader logs a warning, once per material, for each parameter override of a material on it that the new
         * shader lacks or types differently: the override is kept and ignored.
         */
        template <typename T>
        bool Replace(AssetHandle<T> handle, AssetData<T> data)
        {
            using Data = AssetData<T>;
            if (const std::string problem = Detail::PrepareAsset(data); !problem.empty())
            {
                Refuse(Detail::AssetTraits<T>::name, "replace", problem);
                return false;
            }
            Detail::AssetBox box(new Data(std::move(data)), &Detail::DestroyAssetData<Data>);
            return ReplaceSlot(Detail::AssetTraits<T>::kind, {handle.index, handle.generation}, std::move(box));
        }

        /**
         * @brief Frees the asset `handle` names now, whoever owns it. Every handle to it becomes stale, and loading
         * its path again makes a new asset. False if the handle does not resolve.
         */
        template <typename T>
        bool Unload(AssetHandle<T> handle)
        {
            return UnloadSlot(Detail::AssetTraits<T>::kind, {handle.index, handle.generation});
        }

        /**
         * @brief The data `handle` names, or null if the handle is empty or stale. The pointer is valid until the
         * asset is replaced or freed.
         */
        template <typename T>
        [[nodiscard]] const AssetData<T>* TryGet(AssetHandle<T> handle) const
        {
            return static_cast<const AssetData<T>*>(
                Find(Detail::AssetTraits<T>::kind, {handle.index, handle.generation}));
        }

        /** @brief Whether `handle` names a live asset: false for an empty or stale handle. */
        template <typename T>
        [[nodiscard]] bool IsValid(AssetHandle<T> handle) const
        {
            return Find(Detail::AssetTraits<T>::kind, {handle.index, handle.generation}) != nullptr;
        }

        /**
         * @brief The asset's revision: 1 when it is loaded or added, one more on each Replace, each material set and
         * each Reset that removes an override. Users that keep a copy made from the data (the renderer's GPU side)
         * compare it to know when to remake it. Zero if the handle does not resolve.
         */
        template <typename T>
        [[nodiscard]] std::uint32_t Revision(AssetHandle<T> handle) const
        {
            return RevisionOf(Detail::AssetTraits<T>::kind, {handle.index, handle.generation});
        }

        /**
         * @brief The material parameters of the shader `handle` names, each with its type and parameter default, read
         * from its container alone (ShaderData::parameters). For an editor, or anything showing what a material
         * inherits. Empty if the handle does not resolve. Valid until the shader is replaced or freed.
         */
        [[nodiscard]] std::span<const ShaderParameter> GetShaderParameters(AssetHandle<Shader> handle) const;

        /**
         * @name Material edits
         * Make and edit materials through the store, so C++ and C# agree on names, types and revisions (ADR-0024). A
         * material stores only its parameter overrides; every other parameter reads its shader's parameter default.
         *
         * Each set bumps the material's revision. A parameter set checks the name and type against the shader's
         * parameters (GetShaderParameters): an unknown name or a wrong type is logged once per material and ignored,
         * and the set returns false. A set on a material handle that does not resolve is logged and returns false. A
         * colour is a `vec4` parameter, in linear values.
         * @{
         */

        /**
         * @brief A new material drawing with `shader`, owned by `owner`: opaque, single-sided, with no parameter
         * overrides. Empty, with a logged error, if the shader handle does not resolve.
         */
        [[nodiscard]] AssetHandle<Material> CreateMaterial(AssetHandle<Shader> shader, AssetOwner owner);

        /**
         * @brief A new material copying `material`'s shader, blend mode, alpha cutoff, double-sidedness and parameter
         * overrides, owned by `owner`. Only overrides are copied, not resolved defaults, so the clone's unset
         * parameters still follow the shader's defaults. Empty, with a logged error, if `material` does not resolve.
         */
        [[nodiscard]] AssetHandle<Material> CloneMaterial(AssetHandle<Material> material, AssetOwner owner);

        /** @brief Sets a `float` parameter. */
        bool SetFloat(AssetHandle<Material> material, std::string_view name, float value);
        /** @brief Sets a `vec2` parameter. */
        bool SetVector2(AssetHandle<Material> material, std::string_view name, Math::Vec2 value);
        /** @brief Sets a `vec3` parameter. */
        bool SetVector3(AssetHandle<Material> material, std::string_view name, Math::Vec3 value);
        /** @brief Sets a `vec4` parameter. */
        bool SetVector4(AssetHandle<Material> material, std::string_view name, Math::Vec4 value);
        /** @brief Sets a `vec4` parameter to a colour's linear red, green, blue and alpha. */
        bool SetColour(AssetHandle<Material> material, std::string_view name, Colour value);
        /** @brief Sets a texture parameter to `image`, read with `sampler`. */
        bool SetTexture(AssetHandle<Material> material, std::string_view name, AssetHandle<Image> image,
                        SamplerDescription sampler = {});
        /** @brief Sets how the material's pixels combine with what is behind them. */
        bool SetBlendMode(AssetHandle<Material> material, BlendMode blendMode);
        /** @brief Sets the alpha below which a `Masked` material discards a pixel. */
        bool SetAlphaCutoff(AssetHandle<Material> material, float alphaCutoff);
        /** @brief Sets whether both faces of the material are drawn. */
        bool SetDoubleSided(AssetHandle<Material> material, bool doubleSided);

        /**
         * @brief The parameter's effective value: the material's override, else the shader's parameter default.
         * Empty only if the material does not resolve, its shader lacks the name, or the parameter is of another type.
         * A texture parameter left unset reads an empty image (the shader's default texture) with the default sampler.
         * Reads never bump the revision.
         */
        [[nodiscard]] std::optional<float> TryGetFloat(AssetHandle<Material> material, std::string_view name) const;
        /** @copydoc TryGetFloat */
        [[nodiscard]] std::optional<Math::Vec2> TryGetVector2(AssetHandle<Material> material,
                                                              std::string_view name) const;
        /** @copydoc TryGetFloat */
        [[nodiscard]] std::optional<Math::Vec3> TryGetVector3(AssetHandle<Material> material,
                                                              std::string_view name) const;
        /** @copydoc TryGetFloat */
        [[nodiscard]] std::optional<Math::Vec4> TryGetVector4(AssetHandle<Material> material,
                                                              std::string_view name) const;
        /** @copydoc TryGetFloat */
        [[nodiscard]] std::optional<Colour> TryGetColour(AssetHandle<Material> material, std::string_view name) const;
        /** @copydoc TryGetFloat */
        [[nodiscard]] std::optional<MaterialTexture> TryGetTexture(AssetHandle<Material> material,
                                                                   std::string_view name) const;

        /**
         * @brief The material's own override of the parameter, if it sets one of this type. It does not consult the
         * shader, so it also reads an override the shader no longer has.
         */
        [[nodiscard]] std::optional<float> TryGetOverrideFloat(AssetHandle<Material> material,
                                                               std::string_view name) const;
        /** @copydoc TryGetOverrideFloat */
        [[nodiscard]] std::optional<Math::Vec2> TryGetOverrideVector2(AssetHandle<Material> material,
                                                                      std::string_view name) const;
        /** @copydoc TryGetOverrideFloat */
        [[nodiscard]] std::optional<Math::Vec3> TryGetOverrideVector3(AssetHandle<Material> material,
                                                                      std::string_view name) const;
        /** @copydoc TryGetOverrideFloat */
        [[nodiscard]] std::optional<Math::Vec4> TryGetOverrideVector4(AssetHandle<Material> material,
                                                                      std::string_view name) const;
        /** @copydoc TryGetOverrideFloat */
        [[nodiscard]] std::optional<Colour> TryGetOverrideColour(AssetHandle<Material> material,
                                                                 std::string_view name) const;
        /** @copydoc TryGetOverrideFloat */
        [[nodiscard]] std::optional<MaterialTexture> TryGetOverrideTexture(AssetHandle<Material> material,
                                                                           std::string_view name) const;

        /** @brief Whether the material has an override of the parameter. */
        [[nodiscard]] bool IsSet(AssetHandle<Material> material, std::string_view name) const;

        /**
         * @brief Removes the material's override of the parameter, so it reads the shader's default again. True, and
         * the revision bumped, only if there was an override to remove.
         */
        bool Reset(AssetHandle<Material> material, std::string_view name);
        /** @} */

        /** @brief The asset root relative paths resolve against, absolute. */
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;

      private:
        Detail::AssetSlot LoadSlot(Detail::AssetKind kind, std::string_view path, AssetOwner owner,
                                   std::optional<LoadDiagnostic>& diagnostic);
        Detail::AssetSlot AddSlot(Detail::AssetKind kind, Detail::AssetBox data, AssetOwner owner);
        bool ReplaceSlot(Detail::AssetKind kind, Detail::AssetSlot slot, Detail::AssetBox data);
        bool UnloadSlot(Detail::AssetKind kind, Detail::AssetSlot slot);
        [[nodiscard]] const void* Find(Detail::AssetKind kind, Detail::AssetSlot slot) const;
        [[nodiscard]] std::uint32_t RevisionOf(Detail::AssetKind kind, Detail::AssetSlot slot) const;
        void Refuse(std::string_view assetName, std::string_view action, const std::string& problem);
        bool SetParameter(AssetHandle<Material> material, std::string_view name, MaterialValue value);
        [[nodiscard]] MaterialData* EditMaterial(AssetHandle<Material> material, std::string_view action);
        [[nodiscard]] std::optional<MaterialValue> EffectiveValue(AssetHandle<Material> material, std::string_view name,
                                                                  ShaderParameterType type) const;
        [[nodiscard]] std::optional<MaterialValue> OverrideValue(AssetHandle<Material> material, std::string_view name,
                                                                 ShaderParameterType type) const;
        void WarnOverridesShaderLacks(AssetHandle<Shader> shader);

        // Shared with the scenes that own assets, which hold it weakly: a scene that ends after the store frees
        // nothing.
        std::shared_ptr<Detail::AssetStoreState> m_state;
    };

    /**
     * @brief Remembers which asset handles have been reported, so code that meets a stale or empty handle every frame
     * (a component naming a freed mesh) logs it once per handle rather than once per frame.
     */
    class StaleHandleLog
    {
      public:
        /** @brief True the first time `handle` is asked about, false every time after. */
        template <typename T>
        [[nodiscard]] bool ShouldLog(AssetHandle<T> handle)
        {
            const std::uint64_t key = (std::uint64_t{handle.generation} << 32u) | handle.index;
            return m_seen[static_cast<std::size_t>(Detail::AssetTraits<T>::kind)].insert(key).second;
        }

        /** @brief Forgets every handle, so each is logged again the next time it is met. */
        void Clear() noexcept
        {
            for (std::unordered_set<std::uint64_t>& seen : m_seen)
            {
                seen.clear();
            }
        }

      private:
        std::array<std::unordered_set<std::uint64_t>, Detail::AssetKindCount> m_seen;
    };
} // namespace Fenrir
