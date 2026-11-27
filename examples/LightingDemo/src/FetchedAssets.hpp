#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Fenrir
{
    class Scene;
} // namespace Fenrir

// LightingDemo's fetched assets (ADR-0052): the files fetched-assets.json lists, which FENRIR_FETCH_DEMO_ASSETS
// downloads at configure time into FENRIR_DEMO_ASSET_CACHE, and each build links into the asset root's folder
// LIGHTING_DEMO_FETCHED_FOLDER. The build gives the app the cache as LIGHTING_DEMO_FETCHED_ASSETS when the option is
// on.
namespace LightingDemo
{
    /**
     * @brief The asset path the store loads the fetched file `path` (its manifest path, `DamagedHelmet/...`) from,
     * `fetched/DamagedHelmet/...` under the asset root; none when the build did not fetch the assets, the file is not
     * in the cache, or this is a smoke run, which uses committed assets only (FENRIR_DEMO_SMOKE).
     */
    [[nodiscard]] std::optional<std::string> FetchedAsset(std::string_view path);

    /**
     * @brief Says on screen, for this frame, that the scene `sceneName` needs fetched assets it does not have, naming
     * FENRIR_FETCH_DEMO_ASSETS, through the scene's DebugDraw.
     */
    void ShowMissingAssets(Fenrir::Scene& scene, std::string_view sceneName);
} // namespace LightingDemo
