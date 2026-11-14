#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UIEvent.hpp"

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
        /** @brief The player's name, shown over the vitals. */
        std::string playerName;
        /** @brief Whether the HUD shows at all. */
        bool shown = true;
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

        /** @brief Adds a hit marker at the centre of the root, which shows, and leaves through its outro. */
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
        /** @brief The top element as last shown or hidden, and whether it was shown. */
        Fenrir::ElementId m_top;
        bool m_shown = true;
    };

    /** @brief Where the pause menu draws. */
    struct MenuConfig
    {
        /** @brief The menu root's 2D target; null draws on the main window's overlay, on MenuLayer. */
        std::shared_ptr<Fenrir::UITarget> target;
    };

    /** @brief The overlay layer the menu draws on: over the HUD's, under the ImGui overlay's. */
    inline constexpr std::int32_t MenuLayer = 10;

    /** @brief The menu root's input priority: over the HUD's, so its elements see input first, under ImGui's. */
    inline constexpr int MenuInputPriority = 10;

    /** @brief What the settings panel sets, kept by the menu while its elements come and go. */
    struct Settings
    {
        /** @brief The volume, 0 to 100. The engine has no audio yet, so the demo only shows it. */
        float volume = 80.0f;
        /** @brief The window camera's vertical field of view, in degrees. */
        float fieldOfView = 60.0f;
        /** @brief Moving the mouse up turns the free-fly camera down. */
        bool invertLook = false;
        /** @brief Whether the HUD shows. */
        bool showHud = true;
        /** @brief Whether the left stick moves a pointer over the menu instead of moving the focus. */
        bool virtualCursor = false;
        /** @brief The name the HUD shows over the vitals. */
        std::string playerName = "Player";
    };

    /**
     * @brief The pause menu, an app resource: a UI root on the window overlay (or MenuConfig::target) over the HUD,
     * styled by `ui/menu.css` with hot reload on. Opening it loads `ui/menu.ui`, a frosted panel with Resume, Settings
     * and Quit, which comes in through `:intro`; Settings loads `ui/settings.ui` beside it; closing removes them, and
     * they leave through `:outro`. The menu is game code (ADR-0063): the game decides when it is there, and its Update,
     * the menu's system, reads the root's UI events (ADR-0065) and keeps the Settings. While it is open, its
     * full-window backing takes pointer input, so the camera does not turn under it.
     */
    class Menu
    {
      public:
        /** @brief Makes the root and loads the stylesheet; the menu starts closed. */
        Menu(Fenrir::App& app, const MenuConfig& config);
        Menu(const Menu&) = delete;
        Menu& operator=(const Menu&) = delete;
        Menu(Menu&&) = delete;
        Menu& operator=(Menu&&) = delete;
        /** @brief Destroys the root. */
        ~Menu();

        /** @brief Loads the menu's document, showing the settings as they are kept. Nothing when it is open. */
        void Open();
        /** @brief Removes the menu and its settings panel through their outros, and takes the focus from them. */
        void Close();
        /** @brief Whether the menu is open. */
        [[nodiscard]] bool IsOpen() const noexcept
        {
            return m_open;
        }

        /**
         * @brief The menu's system, run once a frame: Escape opens or closes it, and the root's UI events since the
         * last frame drive its buttons and keep what its controls set.
         */
        void Update();

        /** @brief The settings, as the settings panel last set them. */
        [[nodiscard]] const Settings& GetSettings() const noexcept
        {
            return m_settings;
        }

        /** @brief The stylesheet's file, absolute, for the user to edit while the demo runs. */
        [[nodiscard]] const std::string& StylesheetPath() const noexcept
        {
            return m_stylesheetPath;
        }

        /** @brief The menu's root. */
        [[nodiscard]] Fenrir::UIRoot& Root() const noexcept
        {
            return *m_root;
        }

      private:
        void OpenSettings();
        void CloseSettings();
        /** @brief Steps back: closes the settings panel when it is open, and the menu otherwise. */
        void Back();
        /** @brief Writes the kept settings into the settings panel's controls. */
        void ShowSettings();
        void OnEvent(const Fenrir::UIEvent& event);
        /** @brief Loads `file` under `parent`, logging an error when it loads with problems. */
        void Load(Fenrir::ElementId parent, const char* file);

        Fenrir::App& m_app;
        Fenrir::UIRoot* m_root = nullptr;
        std::string m_files;
        std::string m_stylesheetPath;
        Fenrir::StylesheetId m_stylesheet;
        Fenrir::EventReader<Fenrir::UIEvent> m_events;
        Settings m_settings;
        bool m_open = false;
        bool m_settingsOpen = false;
    };

    /** @brief The room: a lit room with the player, a top-down camera for the minimap and the HUD's system. */
    [[nodiscard]] FenrirDemo::DemoScene Room();

    /**
     * @brief The app as main runs it: its scenes, the ImGui overlay for its switches, and FenrirUI, the Hud and the
     * Menu installed after the renderer. The HUD draws where `hud` says, and the menu where `menu` says.
     */
    [[nodiscard]] FenrirDemo::DemoConfig MakeDemoConfig(HudConfig hud = {}, MenuConfig menu = {});
} // namespace UIDemo
