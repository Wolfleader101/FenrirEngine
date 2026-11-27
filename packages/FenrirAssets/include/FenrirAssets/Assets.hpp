#pragma once

#include <filesystem>
#include <string_view>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/Move.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirAssets/SpawnNodes.hpp"

namespace Fenrir
{
    class App;
    class TypeRegistry;
    // Only named here: FenrirAssetsScripting defines Assets::ScriptingExtension().
    struct ScriptingExtension;

    /**
     * @brief The owner of the asset store FenrirAssets installs: destroying it frees every asset. Move-only. Destroy
     * it before the App, and keep it alive while the App runs.
     */
    class Assets
    {
      public:
        /**
         * @brief Turns assets on for `app`: adds the AssetStore app resource, with its asset root from `config`. It
         * cannot fail. Assets are loaded, added and freed only when called; with AssetsConfig::hotReload on, it adds
         * one sequential system at SchedulePriority::PreUpdate that reloads, between frames, each file the store loaded
         * that changed on disk (AssetStore::Reloaded), and with it off it adds none.
         */
        [[nodiscard]] static Assets Install(App& app, AssetsConfig config = {});

        /**
         * @brief What the asset store gives C# (ADR-0029): the `Fenrir.Assets` contract, with the native functions
         * behind its `Assets`, `Materials` and `Shaders` classes (loading, adding and replacing meshes and images,
         * render targets, the material edits and shader parameters).
         *
         * It needs no owner: its functions read the app the running scripts serve, so scripts always load. With no
         * asset store, or for a stale or empty handle, they do nothing and return false, zero or the empty handle. In
         * debug builds each such case is warned about once per scripting install for each place in
         * FenrirAssetsScripting's native code that notices it, not once per C# call site.
         *
         * Defined by the FenrirAssetsScripting library, not FenrirAssets (which never links FenrirScripting), so it
         * exists only in a build with assets and scripting on. Include "FenrirAssetsScripting/AssetsScripting.hpp" and
         * link FenrirAssetsScripting to call it.
         */
        [[nodiscard]] static Fenrir::ScriptingExtension ScriptingExtension();

        /**
         * @brief Moves or renames the file at the asset path `from` to the asset path `to`, both under `root`, and
         * rewrites every reference to it across the root's envelopes and scene files (ADR-0076 §5, ADR-0079 §6). It
         * needs no running store, and `registry` holds the types of the files it scans.
         *
         * `from` must name a file, and `to` must name nothing yet: a destination that exists is refused, and so is a
         * path that is absolute, climbs through `..`, names a sub-asset (`#`) or a `builtin:` asset, or leaves the
         * root. Every `.json5` file under the root is read with ReadText, and each string a `reference` field of its
         * entries' types holds that is `from`, or `from#name` for a sub-asset of it, is rewritten to `to` or `to#name`;
         * the moved file's own references included. An envelope's entries are its top level's and each `sub` entry's.
         * A `.scene.json5` file's are its `resources`, each block's `components` and each `overrides` entry's
         * `components` (ADR-0078, ADR-0080), and each block's `instance` path is matched and rewritten too. The
         * registry's descriptions find the fields. A changed file is written back atomically through WriteText from its
         * edited tree, so its comments are kept and a file the engine wrote changes in those strings alone.
         *
         * MoveResult::unscanned lists each file left as it was: one that does not parse, is no envelope, or names a
         * type `registry` lacks in its top level or a sub-asset; and a scene file that does not parse or is no scene
         * envelope. It also lists a scene file with an entry of a type `registry` lacks; that file's other entries
         * and its `instance` paths are still rewritten, so it can be in MoveResult::changed too.
         *
         * A reference is matched as Load resolves it, so `./from` and one spelled with backslashes are rewritten too,
         * and a rename that changes only the case is allowed where the disk ignores case. The file is moved first, so a
         * failure there writes nothing. Fields are found by the types' current
         * descriptions, so a reference in a file at an older version is found where its field kept its name or a
         * former one.
         *
         * @param dryRun When true, nothing is written or moved, and the result lists the same changes.
         */
        [[nodiscard]] static MoveResult Move(const std::filesystem::path& root, std::string_view from,
                                             std::string_view to, const TypeRegistry& registry, bool dryRun = false);

        Assets(Assets&& other) noexcept;
        Assets& operator=(Assets&& other) noexcept;
        Assets(const Assets&) = delete;
        Assets& operator=(const Assets&) = delete;
        ~Assets();

        /** @brief True unless this owner was moved from. */
        [[nodiscard]] bool IsValid() const noexcept;

        /** @brief The asset store this owner installed, the App's AssetStore resource. The owner must be valid. */
        [[nodiscard]] AssetStore& Store() const;

      private:
        explicit Assets(App& app) noexcept;

        void Shutdown() noexcept;

        App* m_app = nullptr;
    };
} // namespace Fenrir
