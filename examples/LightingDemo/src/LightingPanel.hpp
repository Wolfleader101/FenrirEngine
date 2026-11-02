#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirRenderer/Components.hpp"

namespace Fenrir
{
    class App;
    class Scene;
} // namespace Fenrir

// The lighting panel every LightingDemo scene shows: an ImGui window for the cameras' exposure, tonemapper and bloom
// and the scene's sky, with keys for the same, which the smoke runs press.
namespace LightingDemo
{
    /** @brief A panorama the panel can switch the scene's sky to, and the name it shows. */
    struct NamedPanorama
    {
        std::string name;
        Fenrir::AssetHandle<Fenrir::Image> image;
    };

    /** @brief What the panel and its keys have set, kept in each scene's own scene resource. */
    struct LightingSettings
    {
        /** @brief Every camera's exposure, in EV stops. */
        float exposure = 0.0f;
        Fenrir::Tonemapper tonemapper = Fenrir::Tonemapper::AgX;
        /** @brief Every camera's bloom; B sets its intensity to 0 and back. */
        Fenrir::Bloom bloom;
        /** @brief The panoramas the sky can be; empty leaves the scene's Sky alone. */
        std::vector<NamedPanorama> panoramas;
        /** @brief Which of `panoramas` the sky is. */
        std::size_t panorama = 0;
        float skyIntensity = 1.0f;
        /** @brief Radians about world +Y. */
        float skyRotation = 0.0f;
    };

    /**
     * @brief The keys the panel answers, one line for a scene's help panel: T, E, B and, with more than one panorama,
     * S.
     */
    [[nodiscard]] std::string LightingKeyHelp(bool panoramaSwitch);

    /**
     * @brief Runs the panel for one frame, from a scene's `update`: applies its keys (T the next tonemapper, E the next
     * exposure, B bloom off or back on, S the next panorama), draws the "Lighting" ImGui window, with `sceneControls`
     * drawn in it under the panel's own, then gives every Camera in `scene` the exposure, tonemapper and bloom, and the
     * scene's Sky the chosen panorama, its intensity and its rotation.
     */
    void RunLightingPanel(Fenrir::App& app, Fenrir::Scene& scene, LightingSettings& settings,
                          const std::function<void()>& sceneControls = {});
} // namespace LightingDemo
