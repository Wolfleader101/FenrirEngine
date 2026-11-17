#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UI.hpp"

// The demo UI kit (GLOSSARY.md, ADR-0071): the demo panel on the right and the help at the bottom left, in one theme,
// over FenrirUI. Demo code, not an engine package, and it names no scaffold type: the demo scaffold feeds it, so it can
// be lifted into a package if a game ever wants it.

namespace Fenrir
{
    class App;
    class UIRoot;
    class UITarget;
} // namespace Fenrir

namespace FenrirDemo
{
    /**
     * @brief The overlay layer the kit's root draws on: over every demo app's own roots, under the ImGui overlay's
     * (ImGuiOverlayLayer) while that is there. Debug screen text and on-screen messages draw over it.
     */
    inline constexpr std::int32_t DemoUILayer = 900;

    /** @brief The kit's root's input priority: it sees input before every demo app's own roots, after ImGui's. */
    inline constexpr int DemoUIRouterPriority = 900;

    /** @brief A key row (GLOSSARY.md): one line of the help, the keys and what they do. */
    struct DemoKeyRow
    {
        /** @brief The keys, as the help shows them ("PageDown/PageUp"); empty for a line with no keys of its own. */
        std::string keys;
        /** @brief What they do. */
        std::string what;

        friend bool operator==(const DemoKeyRow&, const DemoKeyRow&) = default;
    };

    /** @brief How the kit is made. */
    struct DemoUIConfig
    {
        /**
         * @brief The folder `ui/demo.ui` and `ui/demo.css` are read from: the source tree's while it is there, for hot
         * reload to show an edit, or the asset root.
         */
        std::filesystem::path folder;
        /** @brief Poll the kit's files and load again what changed (off in a smoke run, a check of what was built). */
        bool hotReload = true;
        /** @brief The root's 2D target: the tests' seam, which captures it. Null draws on the main window's overlay. */
        std::shared_ptr<Fenrir::UITarget> target;
    };

    class DemoUI;

    /**
     * @brief A row of a demo section, as a handle. It names nothing once its section is gone (a scene section after
     * DemoUI::ClearScene), and then does nothing. Valid only while the DemoUI that made it is.
     */
    class DemoRow
    {
      public:
        DemoRow() = default;

        /** @brief Shows or hides the row; a hidden row takes no room. */
        void SetShown(bool shown) const;
        /** @brief Greys the row out and keeps its controls from taking input. */
        void SetDisabled(bool disabled) const;
        /** @brief Whether the row is still in the kit. */
        [[nodiscard]] bool IsValid() const;

      protected:
        DemoRow(DemoUI* ui, std::uint32_t id) : m_ui(ui), m_id(id)
        {
        }

        DemoUI* m_ui = nullptr;
        std::uint32_t m_id = 0;

        friend class DemoSection;
    };

    /** @brief A read-only line of a demo section (DemoSection::Text), which can be changed. */
    class DemoTextRow : public DemoRow
    {
      public:
        DemoTextRow() = default;

        /** @brief Changes the line's text. */
        void Set(std::string_view line) const;

      private:
        using DemoRow::DemoRow;

        friend class DemoSection;
    };

    /**
     * @brief A section of the demo panel, as a handle: a heading that collapses and opens it, and its rows in the order
     * they were added. It names nothing once it is gone (a scene section after DemoUI::ClearScene), and then does
     * nothing. Valid only while the DemoUI that made it is.
     */
    class DemoSection
    {
      public:
        DemoSection() = default;

        /** @brief Adds a read-only line. */
        DemoTextRow Text(std::string line) const;

        /** @brief Opens or collapses the section; it starts open. */
        void SetOpen(bool open) const;
        /** @brief Whether the section is open; false once it is gone. */
        [[nodiscard]] bool IsOpen() const;
        /** @brief Whether the section is still in the kit. */
        [[nodiscard]] bool IsValid() const;
        /**
         * @brief The section's element in the kit's root, or nothing once it is gone. A reloaded `demo.ui` makes the
         * kit's elements again, under new ids, so look it up again rather than keeping it.
         */
        [[nodiscard]] Fenrir::ElementId Element() const;

      private:
        DemoSection(DemoUI* ui, std::uint32_t id) : m_ui(ui), m_id(id)
        {
        }

        DemoUI* m_ui = nullptr;
        std::uint32_t m_id = 0;

        friend class DemoUI;
    };

    /**
     * @brief The demo UI kit, an app resource the demo scaffold adds once FenrirUI is installed: one UIRoot on the main
     * window's overlay at DemoUILayer and DemoUIRouterPriority holding `ui/demo.ui`, styled by `ui/demo.css`.
     *
     * - **The demo panel**, on the right, under a title: the app's sections, which stay, then the scene's, which
     *   ClearScene removes. It scrolls when it is taller than the window.
     * - **The help**, at the bottom left: a section that starts open, with a heading line and key rows (SetHelp).
     * - **Input:** only the panels' boxes take the pointer, so a right-drag or a click beside them reaches the scene.
     *   The root takes no gamepad input (UIRootConfig::gamepadNavigation off), so a pad always drives the scene.
     * - **F1** hides or shows all of it, unless a text entry of the kit has the focus. Showing it logs the help, as
     *   `Help: ...` lines, and so does a new help while it shows.
     * - **Hot reload:** an edited `demo.css` restyles the kit; an edited `demo.ui` loads again, and the kit makes its
     *   sections and the help again in it.
     *
     * Neither copied nor moved: it is made in place as an app resource, and its handles point at it.
     */
    class DemoUI
    {
      public:
        /** @brief Makes the root, loads the stylesheet and the document; the kit starts shown. */
        DemoUI(Fenrir::App& app, DemoUIConfig config);
        DemoUI(const DemoUI&) = delete;
        DemoUI& operator=(const DemoUI&) = delete;
        DemoUI(DemoUI&&) = delete;
        DemoUI& operator=(DemoUI&&) = delete;
        /** @brief Destroys the root. */
        ~DemoUI();

        /** @brief The kit's root. */
        [[nodiscard]] Fenrir::UIRoot& Root() const noexcept;

        /** @brief Shows or hides all of the kit; hidden, it takes no input and gives up the focus. */
        void SetVisible(bool visible);
        /** @brief Whether the kit shows. */
        [[nodiscard]] bool IsVisible() const noexcept
        {
            return m_visible;
        }

        /**
         * @brief Loads the stylesheet at `path` (relative to the asset root, or absolute) into the kit's root after
         * `demo.css`, for a demo that restyles the kit; hot reloaded as `demo.css` is. Logs an error when it does not
         * load cleanly.
         */
        Fenrir::StylesheetId AddStylesheet(const std::string& path);
        /** @brief `demo.css`'s file, absolute, for the user to edit while the demo runs. */
        [[nodiscard]] const std::string& StylesheetPath() const noexcept
        {
            return m_stylesheetPath;
        }

        /** @brief The demo panel's title line, at its top: the app's name. */
        void SetTitle(std::string title);

        /** @brief Adds a section of the app's to the demo panel, after the app's other sections. It stays. */
        DemoSection AppSection(std::string title);
        /** @brief Adds a section of the active scene's to the demo panel, after the app's sections and the scene's. */
        DemoSection SceneSection(std::string title);
        /**
         * @brief Removes every scene section: the scaffold calls it when a scene is left, before its FenrirScene goes,
         * so a row bound to the scene's state never outlives it.
         */
        void ClearScene();

        /**
         * @brief Sets the help: `heading` (the app and the scene), then `rows` in order. Logged as `Help: ...` lines
         * while the kit shows.
         */
        void SetHelp(std::string heading, std::vector<DemoKeyRow> rows);

      private:
        friend class DemoRow;
        friend class DemoTextRow;
        friend class DemoSection;

        /** @brief A row as the kit keeps it, so it can be made again in a reloaded document. */
        struct Row
        {
            std::uint32_t id = 0;
            std::string text;
            bool shown = true;
            bool disabled = false;
            Fenrir::ElementId element;
        };

        /** @brief A section as the kit keeps it. */
        struct Section
        {
            std::uint32_t id = 0;
            std::string title;
            bool scene = false;
            bool open = true;
            std::vector<Row> rows;
            Fenrir::ElementId element;
            Fenrir::ElementId body;
        };

        /** @brief Finds the document's containers again and makes every section, the title and the help in them. */
        void Build();
        DemoSection AddSection(std::string title, bool scene);
        void BuildSection(Section& section);
        void BuildRow(Row& row, Fenrir::ElementId body);
        void BuildHelp();
        void ApplyOpen(const Section& section) const;
        void ApplyRow(const Row& row) const;
        void ApplyVisible() const;
        /** @brief Shows `element` or takes it out of the layout. */
        void Display(Fenrir::ElementId element, bool shown) const;
        void LogHelp() const;
        /** @brief The build stage work: F1 from the game's keyboard, which sees it while the kit has no focus. */
        void PollToggleKey();
        /** @brief A key the kit's focused element saw: F1 toggles the kit, unless that element is a text entry. */
        void OnKey(Fenrir::Key key);

        [[nodiscard]] Section* FindSection(std::uint32_t id);
        [[nodiscard]] Row* FindRow(std::uint32_t id);

        Fenrir::App& m_app;
        DemoUIConfig m_config;
        Fenrir::UIRoot* m_root = nullptr;
        std::string m_stylesheetPath;
        Fenrir::BuildStageHandle m_buildStage;
        bool m_visible = true;
        std::string m_title;
        std::string m_helpHeading;
        std::vector<DemoKeyRow> m_helpRows;
        bool m_helpOpen = true;
        std::vector<Section> m_sections;
        std::uint32_t m_nextId = 1;
        /** @brief The document's containers, found again after a reload. */
        Fenrir::ElementId m_kit;
        Fenrir::ElementId m_panelTitle;
        Fenrir::ElementId m_appSections;
        Fenrir::ElementId m_sceneSections;
        Fenrir::ElementId m_help;
        Fenrir::ElementId m_helpBody;
    };
} // namespace FenrirDemo
