#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
#include "FenrirRenderer/Components.hpp"

namespace Fenrir
{
    class App;
    class Scene;
} // namespace Fenrir

// The lighting panel every LightingDemo scene shows: demo panel sections (the demo UI kit, ADR-0071) for the cameras'
// exposure, tonemapper, bloom, streaks, lens flare and dirt, colour grade, camera effects, ambient occlusion,
// reflections and debug view, the sun's shadows and the scene's sky, with keys for the same, which the smoke runs
// press.
namespace LightingDemo
{
    /** @brief A panorama the panel can switch the scene's sky to, and the name it shows. */
    struct NamedPanorama
    {
        std::string name;
        Fenrir::AssetHandle<Fenrir::Image> image;
    };

    struct LightingSettings;

    /**
     * @brief The shared sections of one scene entry in the demo panel: RunLightingPanel adds them on its first call
     * after the scene's `spawn`, so they follow the scene's own sections, bound to the scene's LightingSettings. They
     * go with the scene's other sections when it is left (DemoUI::ClearScene), before the settings do.
     */
    class LightingPanel
    {
      public:
        /**
         * @brief Adds the sections to the demo panel, once per scene entry, bound to `settings` (which holds this
         * panel), and then shows the light-cluster legend only while the debug view shows the clusters. Does nothing
         * without the kit.
         */
        void Update(Fenrir::App& app, LightingSettings& settings);

      private:
        /** @brief The first section, "Camera": invalid until it is added. */
        FenrirDemo::DemoSection m_camera;
        /** @brief The Debug view section's legend of the light clusters' colours. */
        FenrirDemo::DemoTextRow m_clusterLegend;
    };

    /** @brief What the panel and its keys have set, kept in each scene's own scene resource. */
    struct LightingSettings
    {
        /**
         * @brief Every camera's look: E steps its exposure, T its tonemapper, and B sets its bloom's intensity to 0 and
         * back. Its effects are off unless the scene turns them on, and its lens dirt is none (an intensity of 0)
         * unless the scene sets one; a scene that sets no dirt image gets the generated one (AddLensDirt).
         */
        Fenrir::CameraLook look{.lensFlare = {.dirtIntensity = 0.0f}};
        /** @brief Every camera's ambient occlusion, on in the demo; O turns it off and back on. */
        Fenrir::AmbientOcclusion ambientOcclusion{true};
        /** @brief Every camera's screen-space reflections, off unless the scene turns them on. */
        Fenrir::Reflections reflections;
        /**
         * @brief What the cameras show: the lit scene, their raw ambient occlusion, the sun's cascades as colours,
         * its shadow atlas, the spot shadow atlas, the light clusters' heat, their reflections or the reflections'
         * confidence; G steps through them.
         */
        Fenrir::CameraDebugView debugView = Fenrir::CameraDebugView::None;
        /**
         * @brief The shadow members every DirectionalLight in the scene is given (its colour and intensity are the
         * scene's own); H turns its shadows off and back on.
         */
        Fenrir::DirectionalLight sunShadows;
        /** @brief The panoramas the sky can be; empty leaves the scene's Sky alone. */
        std::vector<NamedPanorama> panoramas;
        /** @brief Which of `panoramas` the sky is. */
        std::size_t panorama = 0;
        float skyIntensity = 1.0f;
        /** @brief Radians about world +Y. */
        float skyRotation = 0.0f;
        /** @brief The demo panel's sections over these settings, added by RunLightingPanel. */
        LightingPanel panel;
    };

    /**
     * @brief Generates the demo's lens dirt (Fenrir::GenerateLensDirt) into an image the app owns, once, at startup:
     * what every scene's cameras read for their dirt when the scene sets none of its own.
     */
    void AddLensDirt(Fenrir::App& app);

    /** @brief A colour row's binding over `colour`'s red, green and blue, leaving its alpha as it is. */
    [[nodiscard]] FenrirDemo::DemoValue<Fenrir::Math::Vec3> LinearColour(Fenrir::Colour* colour);

    /**
     * @brief The key rows of the keys the panel answers, for a scene's help: T, E, B, O, G, H and, with more than one
     * panorama, S.
     */
    [[nodiscard]] std::vector<FenrirDemo::DemoKeyRow> LightingKeyRows(bool panoramaSwitch);

    /**
     * @brief The same keys as one line, for the scenes whose help is still one line (Sponza, Neon city, Asteroid ring
     * and Forest clearing, until demo UI ticket 11 gives them key rows).
     */
    [[nodiscard]] std::string LightingKeyHelp(bool panoramaSwitch);

    /**
     * @brief Runs the panel for one frame, from a scene's `update`: applies its keys (T the next tonemapper, E the next
     * exposure, B bloom off or back on, O ambient occlusion off or back on, G the next debug view, H the sun's shadows
     * off or back on, S the next panorama), adds the shared sections on the scene's first frame (LightingPanel), then
     * gives every Camera in `scene` the exposure, tonemapper, bloom, streaks, lens flare and dirt, chromatic
     * aberration, vignette, film grain, colour grade, ambient occlusion, reflections and debug view, every
     * DirectionalLight the shadow settings, and the scene's Sky the chosen panorama, its intensity and its rotation.
     * `settings` must be the scene's own, in a scene resource, since the sections are bound to it.
     */
    void RunLightingPanel(Fenrir::App& app, Fenrir::Scene& scene, LightingSettings& settings);
} // namespace LightingDemo
