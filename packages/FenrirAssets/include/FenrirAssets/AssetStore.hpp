#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/FontData.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirTypes/Describe.hpp"

namespace Fenrir
{
    class ILogger;
    class Scene;
    class TypeRegistry;
    /** @brief FenrirAssets's tests' way inside the store (Tests/AssetsProbe.hpp): no part of its interface. */
    struct AssetsProbe;

    namespace Detail
    {
        /** @brief What the store set up. Defined in FenrirAssets's private headers; nothing else can use it. */
        struct AssetStoreState;
        struct AssetRecord;

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

        /** @brief What a check found that leaves the data usable, one message each, logged as warnings. */
        using AssetWarnings = std::vector<std::string>;

        /**
         * @name Checks before the store takes data
         * Each checks one kind of data and completes what the store computes (a mesh's bounds and skin boxes, a
         * skeleton's engine names and signature, a clip's duration and event order). It returns an empty string when
         * the data is usable, else why it is not, and adds to `warnings` what is usable but suspect.
         * @{
         */
        [[nodiscard]] std::string PrepareAsset(MeshData& mesh, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(ImageData& image, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(ShaderData& shader, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(MaterialData& material, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(SkeletonData& skeleton, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(ClipData& clip, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(BoneMaskData& mask, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(InstanceListData& list, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(FontData& font, AssetWarnings& warnings);
        [[nodiscard]] std::string PrepareAsset(Model& model, AssetWarnings& warnings);
        /** @} */
    } // namespace Detail

    /** @brief A rectangle of an image's pixels: its top-left pixel and its size, in pixels, rows counted from the top.
     */
    struct ImageRegion
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        friend constexpr bool operator==(const ImageRegion&, const ImageRegion&) = default;
    };

    /**
     * @brief How Assets::Install sets the asset store up.
     * @version 1
     */
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
        /** @brief The `builtin:` name has no asset registered under it (AssetStore::AddBuiltin) yet. */
        NotRegistered,
        /** @brief The data asset type loaded is not in the store's type registry: its package never registered it. */
        NotADataAsset,
        /**
         * @brief The data asset file's `type` is not the type loaded or one extending it, or is no type the registry
         * knows.
         */
        WrongType,
        /**
         * @brief The path resolves outside the asset root, so it has no root-relative asset path (ADR-0076): an
         * absolute path elsewhere, or a relative one climbing out through `..`. Every kind refuses it.
         */
        OutsideRoot,
    };

    /** @brief Why a load failed, for a log or an editor. */
    struct LoadDiagnostic
    {
        /** @brief The path that was tried: the file resolved against the asset root, with its `#Label` if it had one.
         */
        std::string path;
        /** @brief Why it failed. */
        LoadFailure reason = LoadFailure::FileNotFound;
        /**
         * @brief The importer's own message, or a description of the reason when no importer ran; for a data asset,
         * the located diagnostic that failed it, when one did.
         */
        std::string importerMessage;
    };

    /** @brief How a load went (ADR-0031). */
    enum class LoadStatus : std::uint8_t
    {
        /** @brief Everything in the file loaded. */
        Succeeded,
        /**
         * @brief The asset loaded, but its file's import left units out (a skeleton with its skins and meshes, a skin
         * with its meshes, a clip), LoadResult::failedUnits saying which and why; or a data asset's file had values
         * that did not read, LoadResult::diagnostics saying where.
         */
        Partial,
        /** @brief Nothing loaded; LoadResult::diagnostic says why. */
        Failed,
    };

    /** @brief What AssetStore::Load gives back: the handle, or the diagnostic saying why there is none. */
    template <typename T>
    struct LoadResult
    {
        /** @brief The loaded asset; empty when the load failed. */
        AssetHandle<T> handle;
        /** @brief Why the load failed; empty when it succeeded. */
        std::optional<LoadDiagnostic> diagnostic;
        /**
         * @brief For an asset from a model file, each unit its import left out, saying why; empty when none was. The
         * same for every load of the file's model or sub-assets, until the file is imported again.
         */
        std::vector<std::string> failedUnits;
        /**
         * @brief For a data asset, the located diagnostics of its file's read, each already logged once: those of the
         * read of its text, its upgrade and its fields (an unknown key dropped, a wrong-typed value left at its
         * default), and the store's own about its envelope and its references (a path refused, or naming a missing
         * target or one of the wrong kind or type, each left empty). Kept when the load failed too, as a syntax
         * error's. A later load of a path already loaded gives its first load's again, not logged again, as a model's
         * failedUnits are.
         */
        std::vector<Serialisation::Diagnostic> diagnostics;
        /**
         * @brief For a data asset, whether its fields' read left something out that no diagnostic names, as the bridge
         * counts it. The store resolves every reference, so a reference it left empty has a diagnostic instead.
         */
        bool incomplete = false;

        /** @brief Whether the load gave a handle: it succeeded, wholly or in part. */
        [[nodiscard]] bool Ok() const noexcept
        {
            return !diagnostic.has_value();
        }

        /** @brief Whether the load succeeded, in part, or failed. */
        [[nodiscard]] LoadStatus Status() const noexcept
        {
            if (diagnostic.has_value())
            {
                return LoadStatus::Failed;
            }
            return failedUnits.empty() && diagnostics.empty() && !incomplete ? LoadStatus::Succeeded
                                                                             : LoadStatus::Partial;
        }
    };

    /** @brief How a save went. */
    enum class SaveStatus : std::uint8_t
    {
        /** @brief The file was written, and every reference in it names its asset. */
        Succeeded,
        /**
         * @brief The file was written, but references in it were written empty (`null`), SaveResult::diagnostics saying
         * which: each names an asset with no asset path a file can hold.
         */
        Partial,
        /** @brief Nothing was written, and the file on disk is as it was; SaveResult::diagnostics says why. */
        Failed,
    };

    /** @brief What AssetStore::Save gives back (ADR-0079): how it went, why, and what it wrote empty. */
    struct SaveResult
    {
        /** @brief Whether the file was written, wholly or with references written empty. */
        SaveStatus status = SaveStatus::Succeeded;
        /**
         * @brief Each diagnostic of the save, already logged once, naming the file it was about: why the save was
         * refused, or each reference written empty. A save writes the text a diagnostic would locate, so each is at
         * line 1, column 1, as a node built in code is.
         */
        std::vector<Serialisation::Diagnostic> diagnostics;
        /**
         * @brief How many references were written empty: each names an asset that has no asset path (one added with no
         * name, or freed) or whose path a file cannot hold (an imported asset loaded from outside the asset root).
         */
        std::size_t emptyReferences = 0;
    };

    /**
     * @brief The asset store: every asset the app holds, as plain CPU-side data behind asset handles (ADR-0021). An app
     * resource, made by Assets::Install and reached with `app.GetResource<AssetStore>()`.
     *
     * Main thread only: loading, adding, replacing, unloading and edits (Edit, and the material, clip, bone mask and
     * image edits) must not run alongside anything else that uses the store. Reads (TryGet, IsValid, Revision, the
     * material TryGet calls) may run together from any thread while nothing writes.
     */
    class AssetStore
    {
      public:
        /**
         * @brief A store whose messages go to `logger`, reading data asset types from `types`, both of which must
         * outlive it. Assets::Install makes it, with the app's TypeRegistry.
         */
        AssetStore(ILogger& logger, AssetsConfig config, const TypeRegistry& types);
        ~AssetStore();
        AssetStore(const AssetStore&) = delete;
        AssetStore& operator=(const AssetStore&) = delete;
        AssetStore(AssetStore&&) = delete;
        AssetStore& operator=(AssetStore&&) = delete;

        /**
         * @brief Loads the asset at `path` for `owner`, blocking until it is read. A relative path resolves against
         * the asset root, and `file#Label` names a sub-asset inside the file. An absolute path under the root names the
         * same asset as its root-relative path; a path outside the root, absolute or climbing out through `..`, fails
         * for every kind (LoadFailure::OutsideRoot).
         *
         * A path already loaded as this kind of asset is not read again: it gives back the same handle and adds
         * `owner` to the asset's owners. A failure is logged and gives a diagnostic instead of a handle. Images are
         * read from PNG, JPEG (both RGBA8, sRGB) and HDR (RGBA32F, linear).
         * A cubemap loads from a `.cubemap` file: text naming one face a line, `+X`, `-X`, `+Y`, `-Y`, `+Z` or `-Z`,
         * then the face's image path relative to the file (`+X right.png`), in any order, with `#` comment lines. Its
         * six faces must be square images of one size and format.
         *
         * A model loads from glTF, GLB, FBX or OBJ, and its meshes, materials and images load from the same file by
         * label (`scene.gltf#Mesh0`, see Model). Either way the whole file is imported once: the model and every
         * sub-asset are stored, all owned by `owner`, and a later load of any of them gives the same handle. Loading a
         * model again adds the owner to its sub-assets too. Problems that leave the model usable (an image that failed
         * and was replaced by white, a conflicting colour space, a repeated bone name) are logged as warnings. A glTF
         * file's skins, skeletons and clips are imported into one bind space (ADR-0031); an error in one fails only its
         * smallest unit (a skeleton with its skins and meshes, a skin with its meshes, a clip), logged as an error, and
         * the load is `LoadStatus::Partial`, naming each in `LoadResult::failedUnits`. FBX and OBJ skinned meshes are
         * still left out with a warning.
         *
         * A font loads from a TrueType or OpenType file (`.ttf`, `.otf` or `.ttc`) as its bytes (FontData), which
         * must start as a font file does.
         *
         * A `builtin:` name loads what AssetStore::AddBuiltin registered under it, and fails until something has.
         *
         * A data asset (`T` a data asset type, ADR-0075) loads from a `.asset.json5` file: one object of `format` (1),
         * `type` (the canonical or a former name of its type), `version` (the type's version it was written at) and
         * `data` (its fields), read as text or the tree encoding alike (ADR-0077). Its `type` must be `T` or a type
         * extending it, so an `AssetHandle<WeaponData>` loads a `Sword` file, and `T` must be in the store's type
         * registry (LoadFailure::NotADataAsset). The tree is upgraded from `version` through the type's upgraders,
         * then read tolerantly: an unknown key is warned and dropped, a value that does not read keeps its default with
         * an error, and either makes the load `Partial`, each in LoadResult::diagnostics with its line and column.
         * The file's `sub` maps names, unique within it, to embedded sub-assets of their own `type`, `version` and
         * `data`, and `file#name` loads one (ADR-0076). One read of the file loads its top level and every sub-asset
         * for `owner`, as a model's sub-assets are, and a later load of any gives the same handle and the file's
         * diagnostics again. A name `sub` lacks fails with LoadFailure::LabelNotFound. A sub-asset that does not read,
         * or holds a `sub` of its own, is left out with a located error, making the load of its siblings `Partial`,
         * and loading it by name fails. A file that does not parse, or whose
         * `format` or `type` is wrong, fails. A path the file names (a reference) that is absolute or climbs through
         * `..` is refused, left empty with a located error (ADR-0076). Any other loads before its field is filled,
         * synchronously and for `owner` (ADR-0076 §3): an `AssetHandle<U>` field's target must be of `U`'s kind, and
         * for a data asset a `U` or a type extending it, and a bare `#name` is a sub-asset of the same file. A
         * missing or wrong target leaves the handle empty with one located error naming the field, the path, what was
         * expected and what was found. A file and its sub-assets have their slots before any of their references load,
         * so a reference back to a file still loading gets its handle at once, and files that name each other all
         * load. The file's bytes are read through the store's
         * asset source (ADR-0085), which matches paths case-sensitively; in a development build it warns, once per
         * path, of a path that finds its file only when case is ignored.
         */
        template <typename T>
        [[nodiscard]] LoadResult<T> Load(std::string_view path, AssetOwner owner)
        {
            LoadResult<T> result;
            Detail::AssetSlot slot;
            if constexpr (Detail::AssetTraits<T>::kind == Detail::AssetKind::Data)
            {
                slot = LoadDataSlot(path, owner, Describe<T>::Description(), result.diagnostic, result.diagnostics,
                                    result.incomplete);
            }
            else
            {
                slot = LoadSlot(Detail::AssetTraits<T>::kind, path, owner, result.diagnostic, result.failedUnits);
            }
            result.handle = {slot.index, slot.generation};
            return result;
        }

        /**
         * @brief Adds `data` as a new asset owned by `owner`. It has no path, so nothing else ever shares it. Data
         * that breaks its type's rules is refused with a logged error, and the handle is empty; data that is usable
         * but suspect (a skinned vertex with no weight, a repeated bone name) is taken with a warning. What the store
         * derives (a mesh's bounds and skin boxes, a skeleton's engine names and bind signature, a clip's duration) is
         * computed here.
         *
         * A data asset (`T` a data asset type) is stored as a `T`, whose type must be in the store's type registry; one
         * that is not is refused with a logged error.
         */
        template <typename T>
        [[nodiscard]] AssetHandle<T> Add(AssetData<T> data, AssetOwner owner)
        {
            if constexpr (Detail::AssetTraits<T>::kind == Detail::AssetKind::Data)
            {
                return AddData<T>(std::move(data), owner, std::nullopt);
            }
            else
            {
                return AddEngineAsset<T>(std::move(data), owner);
            }
        }

        /**
         * @brief Adds the data asset `data` for `owner` under the asset path `name` (ADR-0076), so it is keyed like a
         * loaded one: PathOf gives `name` (root-relative, as a load's path is), and a later Load of `name` gives this
         * handle and reads no file. A relative `name` resolves against the asset root and an absolute one under it
         * names its root-relative path. A `name` an asset of this kind has already (loaded, loading or added), or one
         * outside the root, is refused with a logged error, and the handle is empty; so is a `T` the store's type
         * registry lacks. Only data assets are added with a name.
         */
        template <Detail::DataAssetType T>
        [[nodiscard]] AssetHandle<T> Add(AssetData<T> data, AssetOwner owner, std::string_view name)
        {
            return AddData<T>(std::move(data), owner, name);
        }

        /**
         * @brief The data asset `handle` names, to change in place (ADR-0075): the one shared instance every reader
         * sees, with its revision bumped now, so a holder that compares revisions takes the change. Through the handle
         * of a base type it is the instance's base part, as TryGet's is. Null, with a logged error that `action` (what
         * the caller was doing, `"sharpen a sword"`) words, if the handle does not resolve or names a record of a type
         * that is neither `T` nor extends it; the revision then stays. The pointer is valid until the asset is freed,
         * and an edit made through it after a later Edit or Replace is not a revision of its own.
         */
        template <Detail::DataAssetType T>
        [[nodiscard]] T* Edit(AssetHandle<T> handle, std::string_view action)
        {
            return static_cast<T*>(EditData({handle.index, handle.generation}, Describe<T>::Description(), action));
        }

        /**
         * @brief Adds `data` as the built-in asset `name`, owned by the app; `name` starts with `builtin:`. Loading
         * the name gives its handle. A name can be named before it has data: an imported material names
         * `builtin:lit` or `builtin:unlit` whether or not FenrirRenderer has registered them yet, and its handle
         * resolves once they are. Adding a name that already has data replaces it, as Replace does. Data that breaks
         * its type's rules, or a name without the prefix, is refused with a logged error, and the handle is empty.
         */
        template <typename T>
        AssetHandle<T> AddBuiltin(std::string_view name, AssetData<T> data)
        {
            using Data = AssetData<T>;
            Detail::AssetWarnings warnings;
            if (const std::string problem = Detail::PrepareAsset(data, warnings); !problem.empty())
            {
                Refuse(Detail::AssetTraits<T>::name, "add a built-in", problem);
                return {};
            }
            Warn(Detail::AssetTraits<T>::name, "added as a built-in", warnings);
            Detail::AssetBox box(new Data(std::move(data)), &Detail::DestroyAssetData<Data>);
            const Detail::AssetSlot slot = AddBuiltinSlot(Detail::AssetTraits<T>::kind, name, std::move(box));
            return {slot.index, slot.generation};
        }

        /**
         * @brief Swaps the data of the asset `handle` names for `data` and bumps its revision, so every user sees the
         * change. Its owners, path and handle stay. False, with a logged error, if the handle does not resolve or the
         * data breaks its type's rules; the asset is then unchanged. What Add derives is derived here too. Replacing a
         * shader logs a warning, once per material, for each parameter override of a material on it that the new
         * shader lacks or types differently: the override is kept and ignored.
         *
         * A data asset's value is assigned to its one instance, which keeps its address, so a pointer TryGet or Edit
         * gave stays valid and reads `data`. Its record's type must be exactly `T`, never a type `T` is a base of: a
         * `Sword` record is not replaced through an `AssetHandle<WeaponData>`, which is refused with a logged error.
         */
        template <typename T>
        bool Replace(AssetHandle<T> handle, AssetData<T> data)
        {
            if constexpr (Detail::AssetTraits<T>::kind == Detail::AssetKind::Data)
            {
                void* instance = ReplaceData({handle.index, handle.generation}, Describe<T>::Description());
                if (instance != nullptr)
                {
                    *static_cast<T*>(instance) = std::move(data);
                }
                return instance != nullptr;
            }
            else
            {
                return ReplaceEngineAsset<T>(handle, std::move(data));
            }
        }

        /**
         * @brief Writes `pixels` into `region` of the image `image` names and bumps its revision, recording the region
         * so that a user holding a copy of the image (the renderer's texture) can update just it (ImageUpdatesSince):
         * how FenrirText's glyph atlases upload (ADR-0062). `pixels` holds `region.width * region.height` pixels of the
         * image's format, rows top to bottom. False, with a logged error and the image unchanged, if the handle does
         * not resolve, the image is a cubemap or a render target, the region is empty or not inside the image, or the
         * pixel count is wrong.
         */
        bool UpdateImage(AssetHandle<Image> image, ImageRegion region, std::span<const std::byte> pixels);

        /**
         * @brief The regions UpdateImage wrote into `image` after its revision `revision`, oldest first: all that a
         * copy made at that revision must take again. Empty when the image is still at `revision`. None when the copy
         * must take the whole image again: the image was replaced since, `revision` is newer than the image's, or the
         * store no longer remembers that far back (it keeps the regions of the last 256 updates), and when the handle
         * does not resolve.
         */
        [[nodiscard]] std::optional<std::vector<ImageRegion>> ImageUpdatesSince(AssetHandle<Image> image,
                                                                                std::uint32_t revision) const;

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
         *
         * A data asset's is the one shared, read-only instance, of its file's type: a `Sword` read through an
         * `AssetHandle<WeaponData>` is the sword's WeaponData part. Null, too, for a record whose type is not `T` or a
         * type extending it (a handle made from another's numbers), logged once per handle.
         */
        template <typename T>
        [[nodiscard]] const AssetData<T>* TryGet(AssetHandle<T> handle) const
        {
            if constexpr (Detail::AssetTraits<T>::kind == Detail::AssetKind::Data)
            {
                // A data asset type extends at most one other and has no virtual functions (ADR-0075), so its base is
                // at the instance's own address.
                return static_cast<const T*>(FindData({handle.index, handle.generation}, Describe<T>::Description()));
            }
            else
            {
                return static_cast<const AssetData<T>*>(
                    Find(Detail::AssetTraits<T>::kind, {handle.index, handle.generation}));
            }
        }

        /**
         * @brief The asset path of the asset `handle` names (ADR-0076): for a loaded asset, its path relative to the
         * asset root with forward slashes and its extension, then `#` and its label for a sub-asset (`models/hero.glb#
         * Mesh0`); for a built-in, its `builtin:` name, even before its data is registered. Empty for an asset added
         * with no name, and when the handle is empty or stale.
         */
        template <typename T>
        [[nodiscard]] std::optional<std::string> PathOf(AssetHandle<T> handle) const
        {
            return PathOfSlot(Detail::AssetTraits<T>::kind, {handle.index, handle.generation});
        }

        /**
         * @brief Writes the data asset `handle` names back to its file, with every sub-asset of that file, through the
         * envelope writer (ADR-0077, ADR-0079): the envelope's `format`, `type`, `version` (the type's own) and `data`,
         * then `sub`; each value's fields in declaration order with its defaults left out, so the same data writes the
         * same bytes. The comments of the file as the store last read or wrote it are kept, each where its key is. A
         * sub-asset whose record has been freed, or that did not read, is written back as the file had it. Through a
         * sub-asset's handle, the whole file is written.
         *
         * Each reference is written as its target's asset path, a sub-asset's in full as `path#name`. One naming an
         * asset with no path a file can hold is written `null`, with a warning, and counted in
         * SaveResult::emptyReferences.
         *
         * The write is atomic: the text goes to a file beside the target, named as it with `.saving` added, which is
         * then renamed over it, so a save that fails leaves the old file whole. Refused with a logged error, and
         * nothing written, for an empty or stale handle, a record whose type is neither `T` nor one extending it, and
         * an asset with no path (added with no name: save it to a path).
         */
        template <Detail::DataAssetType T>
        SaveResult Save(AssetHandle<T> handle)
        {
            return SaveData({handle.index, handle.generation}, Describe<T>::Description(), std::nullopt);
        }

        /**
         * @brief Writes the data asset `handle` names to `path`, an asset path relative to the asset root, as Save
         * writes its own file: an asset with no path takes `path` as its asset path, so PathOf gives it and a later
         * Load of it gives this handle, while an asset that has a path keeps it, `path` getting a copy. Refused with a
         * logged error, and nothing written, as Save is, and also for `path` absolute, climbing out of the root, naming
         * a sub-asset (`#`), or naming a file another asset of the store is from; and for a sub-asset's handle, which
         * is saved with its file.
         */
        template <Detail::DataAssetType T>
        SaveResult Save(AssetHandle<T> handle, std::string_view path)
        {
            return SaveData({handle.index, handle.generation}, Describe<T>::Description(), path);
        }

        /** @brief Whether `handle` names a live asset: false for an empty or stale handle. */
        template <typename T>
        [[nodiscard]] bool IsValid(AssetHandle<T> handle) const
        {
            return Find(Detail::AssetTraits<T>::kind, {handle.index, handle.generation}) != nullptr;
        }

        /**
         * @brief The asset's revision: 1 when it is loaded or added, one more on each Replace, each Edit, each material
         * set and each Reset that removes an override. Users that keep a copy made from the data (the renderer's GPU
         * side) compare it to know when to remake it. Zero if the handle does not resolve.
         */
        template <typename T>
        [[nodiscard]] std::uint32_t Revision(AssetHandle<T> handle) const
        {
            return RevisionOf(Detail::AssetTraits<T>::kind, {handle.index, handle.generation});
        }

        /**
         * @brief Whether both assets were loaded from one file, as a model's sub-assets are (a skeleton and a clip of
         * one glTF file): how FenrirAnimation knows a clip may bind to a skeleton by node identity (ADR-0031). False
         * when either handle does not resolve, or either asset was added in code rather than loaded.
         */
        template <typename A, typename B>
        [[nodiscard]] bool FromSameFile(AssetHandle<A> a, AssetHandle<B> b) const
        {
            return SameFileOf(Detail::AssetTraits<A>::kind, {a.index, a.generation}, Detail::AssetTraits<B>::kind,
                              {b.index, b.generation});
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

        /**
         * @name Clip event edits
         * A clip's events, edited through the store so that every reader sees the clip's revision move (ADR-0034).
         * Each edit that changes the clip bumps its revision; an edit on a handle that does not resolve is logged and
         * changes nothing.
         * @{
         */

        /**
         * @brief Adds an event named `name` at `time`, placed by time after any events already at that time, and gives
         * its index. Empty, with a logged error, when the clip does not resolve or `time` is not finite or lies outside
         * the clip's duration.
         */
        std::optional<std::uint32_t> AddClipEvent(AssetHandle<Clip> clip, float time, std::string_view name);

        /** @brief Removes the event at `index`; later events move down one. False when there is no such event. */
        bool RemoveClipEvent(AssetHandle<Clip> clip, std::uint32_t index);
        /** @} */

        /**
         * @name Bone mask edits
         * Bone masks made and edited through the store (ADR-0036), each edit bumping the mask's revision. An edit on a
         * handle that does not resolve is logged and changes nothing.
         * @{
         */

        /** @brief A new bone mask with no entries, owned by `owner`; on a layer it covers no bone until one is
         * included. */
        [[nodiscard]] AssetHandle<BoneMask> CreateBoneMask(AssetOwner owner);

        /**
         * @brief Appends an entry giving the bone named `boneName` (by engine name), or its whole subtree, `weight`; it
         * overrides earlier entries for the bones it covers. False, with a logged error, for a non-finite weight.
         */
        bool IncludeInBoneMask(AssetHandle<BoneMask> mask, std::string_view boneName, float weight,
                               MaskScope scope = MaskScope::Bone);

        /** @brief Removes every entry. */
        bool ClearBoneMask(AssetHandle<BoneMask> mask);
        /** @} */

        /**
         * @brief Spawns `model`'s node tree into `scene` (ADR-0028, ADR-0031): a root entity under `parent` (or at the
         * top of the scene if `parent` is null), and below it one entity per model node with its `Name`, its rest
         * transform as its `Transform`, and its parent link. It adds nothing that draws: FenrirRenderer's SpawnModel
         * adds the meshes. Nothing is spawned, and an error logged, if the handle does not resolve.
         */
        SpawnedNodes SpawnNodes(Scene& scene, AssetHandle<Model> model, Entity parent = {});

        /** @brief The asset root relative paths resolve against, absolute. */
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;

        /** @brief The logger the store's messages go to, for code that loads or spawns through it and reports alike. */
        [[nodiscard]] ILogger& Logger() const noexcept;

      private:
        template <typename T>
        [[nodiscard]] AssetHandle<T> AddEngineAsset(AssetData<T> data, AssetOwner owner)
        {
            using Data = AssetData<T>;
            Detail::AssetWarnings warnings;
            if (const std::string problem = Detail::PrepareAsset(data, warnings); !problem.empty())
            {
                Refuse(Detail::AssetTraits<T>::name, "add", problem);
                return {};
            }
            Warn(Detail::AssetTraits<T>::name, "added", warnings);
            Detail::AssetBox box(new Data(std::move(data)), &Detail::DestroyAssetData<Data>);
            const Detail::AssetSlot slot = AddSlot(Detail::AssetTraits<T>::kind, std::move(box), owner);
            return {slot.index, slot.generation};
        }

        template <typename T>
        [[nodiscard]] AssetHandle<T> AddData(T data, AssetOwner owner, std::optional<std::string_view> name)
        {
            Detail::AssetSlot slot;
            if (void* instance = AddDataSlot(Describe<T>::Description(), owner, name, slot))
            {
                *static_cast<T*>(instance) = std::move(data);
            }
            return {slot.index, slot.generation};
        }

        template <typename T>
        bool ReplaceEngineAsset(AssetHandle<T> handle, AssetData<T> data)
        {
            using Data = AssetData<T>;
            Detail::AssetWarnings warnings;
            if (const std::string problem = Detail::PrepareAsset(data, warnings); !problem.empty())
            {
                Refuse(Detail::AssetTraits<T>::name, "replace", problem);
                return false;
            }
            Warn(Detail::AssetTraits<T>::name, "replaced", warnings);
            Detail::AssetBox box(new Data(std::move(data)), &Detail::DestroyAssetData<Data>);
            return ReplaceSlot(Detail::AssetTraits<T>::kind, {handle.index, handle.generation}, std::move(box));
        }

        Detail::AssetSlot LoadSlot(Detail::AssetKind kind, std::string_view path, AssetOwner owner,
                                   std::optional<LoadDiagnostic>& diagnostic, std::vector<std::string>& failedUnits);
        Detail::AssetSlot LoadDataSlot(std::string_view path, AssetOwner owner, const TypeDescription& type,
                                       std::optional<LoadDiagnostic>& diagnostic,
                                       std::vector<Serialisation::Diagnostic>& diagnostics, bool& incomplete);
        Detail::AssetSlot AddSlot(Detail::AssetKind kind, Detail::AssetBox data, AssetOwner owner);
        /**
         * @brief Stores a value-initialised instance of `type` for `owner`, keyed by `name` when there is one, and
         * gives the instance to move the added value into, with its slot in `slot`; null, logged, when refused.
         */
        [[nodiscard]] void* AddDataSlot(const TypeDescription& type, AssetOwner owner,
                                        std::optional<std::string_view> name, Detail::AssetSlot& slot);
        Detail::AssetSlot AddBuiltinSlot(Detail::AssetKind kind, std::string_view name, Detail::AssetBox data);
        Detail::AssetSlot ReserveBuiltin(Detail::AssetKind kind, std::string_view name);
        [[nodiscard]] std::string ImportModelFile(const std::filesystem::path& file, const std::string& fileKey,
                                                  AssetOwner owner);
        void ShareModel(const Model& model, std::uint32_t owner);
        /** @brief Adds `owner` to the data asset in slot `index`, and to every asset its references loaded, in turn. */
        void ShareData(std::uint32_t index, std::uint32_t owner);
        bool ReplaceSlot(Detail::AssetKind kind, Detail::AssetSlot slot, Detail::AssetBox data);
        /**
         * @brief The instance of the data asset `slot` names, its revision bumped, to assign a `type` to; null, logged,
         * when the slot does not resolve or its record is not exactly of `type`.
         */
        [[nodiscard]] void* ReplaceData(Detail::AssetSlot slot, const TypeDescription& type);
        bool UnloadSlot(Detail::AssetKind kind, Detail::AssetSlot slot);
        [[nodiscard]] const void* Find(Detail::AssetKind kind, Detail::AssetSlot slot) const;
        [[nodiscard]] const void* FindData(Detail::AssetSlot slot, const TypeDescription& type) const;
        [[nodiscard]] std::uint32_t RevisionOf(Detail::AssetKind kind, Detail::AssetSlot slot) const;
        [[nodiscard]] std::optional<std::string> PathOfSlot(Detail::AssetKind kind, Detail::AssetSlot slot) const;
        /** @brief Save's untyped part: writes the data asset `slot` names, as `type`, to its file or to `path`. */
        SaveResult SaveData(Detail::AssetSlot slot, const TypeDescription& type, std::optional<std::string_view> path);
        [[nodiscard]] bool SameFileOf(Detail::AssetKind kindA, Detail::AssetSlot a, Detail::AssetKind kindB,
                                      Detail::AssetSlot b) const;
        void Refuse(std::string_view assetName, std::string_view action, const std::string& problem);
        void Warn(std::string_view assetName, std::string_view action, const Detail::AssetWarnings& warnings);
        /**
         * @brief Every edit's core (Edit, Replace of a data asset, and the material, clip, bone mask and image edits):
         * the live record `slot` names, for the caller to change and bump the revision of, or null when the slot does
         * not resolve. That is logged as `stale`, a format of two `{}`: `what`, the edit's own words (its action, or a
         * material set's parameter name), then the kind's word.
         */
        [[nodiscard]] Detail::AssetRecord* EditRecord(
            Detail::AssetKind kind, Detail::AssetSlot slot, std::string_view what,
            std::string_view stale = "Could not {}: the {} handle is empty or stale");
        /** @brief Edit's untyped part: the instance, as `type`, of the data asset `slot` names, its revision bumped. */
        [[nodiscard]] void* EditData(Detail::AssetSlot slot, const TypeDescription& type, std::string_view action);
        bool SetParameter(AssetHandle<Material> material, std::string_view name, MaterialValue value);
        [[nodiscard]] MaterialData* EditMaterial(AssetHandle<Material> material, std::string_view action);
        [[nodiscard]] std::optional<MaterialValue> EffectiveValue(AssetHandle<Material> material, std::string_view name,
                                                                  ShaderParameterType type) const;
        [[nodiscard]] std::optional<MaterialValue> OverrideValue(AssetHandle<Material> material, std::string_view name,
                                                                 ShaderParameterType type) const;
        void WarnOverridesShaderLacks(AssetHandle<Shader> shader);

        friend struct AssetsProbe;

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
