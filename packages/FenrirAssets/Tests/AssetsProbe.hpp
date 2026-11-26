#pragma once

#include <memory>
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
    };
} // namespace Fenrir
