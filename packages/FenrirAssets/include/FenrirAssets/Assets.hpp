#pragma once

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/ShaderData.hpp"

namespace Fenrir
{
    class App;

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
