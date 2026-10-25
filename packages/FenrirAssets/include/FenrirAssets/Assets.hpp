#pragma once

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirAssets/SpawnNodes.hpp"

namespace Fenrir
{
    class App;
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
         * cannot fail, and it adds no systems: assets are loaded, added and freed only when called.
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
