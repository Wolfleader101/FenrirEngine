#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include "AssetSource.hpp"
#include "AssetStoreState.hpp"
#include "FenrirAssets/AssetStore.hpp"

namespace Fenrir
{
    /**
     * @brief The tests' one way inside the asset store: its private asset source seam (ADR-0085). AssetStore names this
     * type as a friend, so none of it is part of the interface the engine sees.
     */
    struct AssetsProbe
    {
        /** @brief Makes `store` read data asset envelopes through `source` from now on, in place of the directory. */
        static void UseSource(AssetStore& store, std::unique_ptr<Detail::AssetSource> source)
        {
            store.m_state->source = std::move(source);
        }

        /** @brief The resolvers `store` writes a data asset's references through, as its envelope writer will. */
        [[nodiscard]] static Serialisation::Resolvers WritingResolvers(const AssetStore& store)
        {
            return Detail::WritingResolvers(*store.m_state);
        }

        /**
         * @brief Reads the scene file `path` into `scene`, which LoadScene makes itself: so a test can watch the
         * scene's components before the load fills it.
         */
        static SceneLoadResult ReadSceneInto(AssetStore& store, Scene& scene, std::string_view path)
        {
            return store.ReadSceneFile(path, [&]() -> Scene& { return scene; });
        }
    };
} // namespace Fenrir
