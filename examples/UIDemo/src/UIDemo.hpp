#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Stylesheet.hpp"

namespace Fenrir
{
    class App;
    class UIRoot;
    class UITarget;
} // namespace Fenrir

// UIDemo (examples/UIDemo/README.md): what a game gets from FenrirUI, shown over a lit room. The HUD is game code over
// the engine's UI (ADR-0063): the room's systems keep the player's components, and push what they hold into the HUD's
// elements each frame (ADR-0065).
namespace UIDemo
{
    /** @brief The player's health, a demo component: the HUD's health bar shows `current` of `max`. */
    struct Health
    {
        float current = 100.0f;
        float max = 100.0f;
    };

    /** @brief The player's stamina, a demo component: it drains while the camera moves and refills while it stands. */
    struct Stamina
    {
        float current = 100.0f;
        float max = 100.0f;
    };

    /** @brief The player's ammo, a demo component: the rounds in the magazine, the magazine's size and the reserve. */
    struct Ammo
    {
        std::int32_t loaded = 30;
        std::int32_t capacity = 30;
        std::int32_t reserve = 90;
    };

    /** @brief Where the HUD draws. */
    struct HudConfig
    {
        /** @brief The HUD root's 2D target; null draws on the main window's overlay, on HudLayer. */
        std::shared_ptr<Fenrir::UITarget> target;
    };

    /** @brief The overlay layer the HUD draws on, under the ImGui overlay's. */
    inline constexpr std::int32_t HudLayer = 0;

    /** @brief What the HUD shows in a frame, pushed by the room's HUD system. */
    struct HudFrame
    {
        Health health;
        Stamina stamina;
        Ammo ammo;
        /** @brief Where the drawing camera looks, in degrees clockwise from north (-Z) seen from above. */
        float heading = 0.0f;
        /** @brief The top-down camera's render target image. */
        Fenrir::AssetHandle<Fenrir::Image> minimap;
    };

    /**
     * @brief The HUD, an app resource: a UI root on the window overlay (or HudConfig::target) holding the document
     * `ui/hud.ui` styled by `ui/hud.css`, with hot reload on. Every element of it takes no pointer input, so the camera
     * still turns under the mouse. Its files come from examples/UIDemo/assets in the source tree while it is there, so
     * an edit there shows at once, and from the asset root otherwise.
     */
    class Hud
    {
      public:
        /** @brief How long a hit marker shows before its outro starts, in seconds. */
        static constexpr float HitMarkerSeconds = 0.25f;

        /** @brief Registers the `compass` element, makes the root and loads the document and its stylesheet. */
        Hud(Fenrir::App& app, const HudConfig& config);
        Hud(const Hud&) = delete;
        Hud& operator=(const Hud&) = delete;
        Hud(Hud&&) = delete;
        Hud& operator=(Hud&&) = delete;
        /** @brief Destroys the root. */
        ~Hud();

        /** @brief Shows `frame`: the bars' values, the ammo counter's text, the compass's heading and the minimap. */
        void Show(const HudFrame& frame);

        /** @brief Adds a hit marker at the centre of the screen, which shows, and leaves through its outro. */
        void AddHitMarker();

        /** @brief Ages the hit markers by `seconds`, removing those past HitMarkerSeconds (their outros run on). */
        void Update(float seconds);

        /** @brief The stylesheet's file, absolute, for the user to edit while the demo runs. */
        [[nodiscard]] const std::string& StylesheetPath() const noexcept
        {
            return m_stylesheetPath;
        }

        /** @brief The HUD's root. */
        [[nodiscard]] Fenrir::UIRoot& Root() const noexcept
        {
            return *m_root;
        }

      private:
        struct HitMarker
        {
            Fenrir::ElementId element;
            float age = 0.0f;
        };

        [[nodiscard]] Fenrir::ElementId Find(const char* id) const;

        Fenrir::App& m_app;
        Fenrir::UIRoot* m_root = nullptr;
        std::string m_stylesheetPath;
        Fenrir::StylesheetId m_stylesheet;
        std::vector<HitMarker> m_hitMarkers;
        /** @brief The minimap's player arrow as last turned, and the transform it was given. */
        Fenrir::ElementId m_arrow;
        std::string m_arrowTransform;
    };

    /** @brief The room: a lit room with the player, a top-down camera for the minimap and the HUD's system. */
    [[nodiscard]] FenrirDemo::DemoScene Room();

    /**
     * @brief The app as main runs it: its scenes, the ImGui overlay for its switches, and FenrirUI and the Hud
     * installed after the renderer. The HUD draws where `hud` says.
     */
    [[nodiscard]] FenrirDemo::DemoConfig MakeDemoConfig(HudConfig hud = {});
} // namespace UIDemo
