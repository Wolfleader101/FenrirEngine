#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"
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

    namespace Detail
    {
        class DemoSwitch;
    } // namespace Detail

    /**
     * @brief What a switch row (GLOSSARY.md) reads and writes: a `T` of the scene's or the app's, by a pointer to it or
     * by a get and set pair. Each frame, in the kit's build stage work, the kit reads it and shows it in the row's
     * control, unless the user is dragging or editing that control; a change from the control is written through it
     * the frame it arrives, before the scenes' Update. So a key that changes the value moves the control the next
     * frame, and the control changes what the keys see.
     *
     * A pointer must outlive the row: a scene section's rows go when the scene is left (DemoUI::ClearScene), before
     * its state does, so a pointer into the scene's state is safe there.
     */
    template <typename T>
    class DemoValue
    {
      public:
        /** @brief Reads and writes `*value`. */
        DemoValue(T* value) // NOLINT(google-explicit-constructor): `&flag` is the common binding.
            : m_get([value] { return *value; }), m_set([value](const T& next) { *value = next; })
        {
        }
        /** @brief Reads through `get` and writes through `set`. */
        DemoValue(std::function<T()> get, std::function<void(const T&)> set)
            : m_get(std::move(get)), m_set(std::move(set))
        {
        }

        /** @brief The value now; a default `T` when there is no getter. */
        [[nodiscard]] T Get() const
        {
            return m_get ? m_get() : T{};
        }
        /** @brief Writes `value`; nothing when there is no setter. */
        void Set(const T& value) const
        {
            if (m_set)
            {
                m_set(value);
            }
        }

      private:
        std::function<T()> m_get;
        std::function<void(const T&)> m_set;
    };

    /** @brief How a range row's slider maps to its value, and how the value is shown. */
    struct DemoRangeOptions
    {
        /**
         * @brief The slider's 0..1 maps to `min * (max / min)^t`, for values that span orders of magnitude. `min` and
         * `max` must be positive; otherwise the kit logs an error and the range is linear.
         */
        bool logarithmic = false;
        /** @brief The step a linear range's value snaps to from `min`; 0 for none. A logarithmic range has none. */
        float step = 0.0f;
        /** @brief Decimal places the value is shown with, beside the slider. */
        int decimals = 2;
        /** @brief The value is an angle in radians (as are `min` and `max`), shown in degrees. */
        bool angle = false;
    };

    /**
     * @brief A row of a demo section, as a handle. It names nothing once its section is gone (a scene section after
     * DemoUI::ClearScene), and then does nothing. Valid only while the DemoUI that made it is.
     */
    class DemoRow
    {
      public:
        DemoRow() = default;

        /** @brief Shows or hides the row; a hidden row takes no room. Rows start shown. */
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
        friend class DemoUI;
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
     * @brief A readout (GLOSSARY.md) of a demo section (DemoSection::Readout): a label and a live value, in two
     * columns.
     */
    class DemoReadout : public DemoRow
    {
      public:
        DemoReadout() = default;

        /**
         * @brief Shows `value`: it changes the value's element only when it differs, so a scene may set it every
         * frame. A pulled readout's function sets it again the next frame.
         */
        void Set(std::string_view value) const;

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
        /** @brief Adds a readout labelled `label`, empty until DemoReadout::Set gives it a value. */
        DemoReadout Readout(std::string label) const;
        /**
         * @brief Adds a readout labelled `label` whose value is pulled from `value` once a frame, in the kit's build
         * stage work (after the scenes' Update). `value` is called while the row is there: a scene section's no
         * longer once the scene is left, so it may hold pointers into the scene's state.
         */
        DemoReadout Readout(std::string label, std::function<std::string()> value) const;
        /** @brief Adds a rule across the section, under `title` when one is given, to group the rows after it. */
        DemoRow Separator(std::string title = {}) const;

        /**
         * @name Switch rows
         * A switch row (GLOSSARY.md) shows a label and a control bound to `value` (DemoValue). Each takes a `hint`,
         * shown in the hint line at the panel's foot while the row is hovered or has the focus.
         * @{
         */
        /** @brief A checkbox over a `bool`. */
        DemoRow Toggle(std::string label, DemoValue<bool> value, std::string hint = {}) const;
        /** @brief A slider over a `float` from `min` to `max`, its value shown beside it. */
        DemoRow Range(std::string label, DemoValue<float> value, float min, float max, DemoRangeOptions options = {},
                      std::string hint = {}) const;
        /** @brief A slider over an `int` from `min` to `max` in steps of one, its value shown beside it. */
        DemoRow Range(std::string label, DemoValue<int> value, int min, int max, std::string hint = {}) const;
        /**
         * @brief One of `options`, by index: a button for each of up to four, the chosen one with the class `active`;
         * beyond four, a previous and next stepper (wrapping) round the chosen option's name.
         */
        DemoRow Choice(std::string label, DemoValue<int> value, std::vector<std::string> options,
                       std::string hint = {}) const;
        /**
         * @brief A linear colour, each component 0 to 1: a swatch of it, encoded for display, over hue, saturation
         * and value sliders.
         */
        DemoRow Colour(std::string label, DemoValue<Fenrir::Math::Vec3> linear, std::string hint = {}) const;
        /** @brief A button captioned `label` that calls `pressed` when clicked. */
        DemoRow Button(std::string label, std::function<void()> pressed, std::string hint = {}) const;
        /**
         * @brief A text entry over a `std::string`, written at each edit. While it has the focus it has the keyboard
         * and text input: the scene's held keys are cancelled, F1 types into it, Escape or a click on the scene gives
         * the keyboard back.
         */
        DemoRow TextEntry(std::string label, DemoValue<std::string> value, std::string hint = {}) const;
        /** @} */

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

        /** @brief Adds a switch row; an empty handle once the section, or the kit, is gone. */
        DemoRow AddSwitch(std::string label, std::shared_ptr<Detail::DemoSwitch> control, std::string hint) const;

        DemoUI* m_ui = nullptr;
        std::uint32_t m_id = 0;

        friend class DemoUI;
    };

    /**
     * @brief The demo UI kit, an app resource the demo scaffold adds once FenrirUI is installed: one UIRoot on the main
     * window's overlay at DemoUILayer and DemoUIRouterPriority holding `ui/demo.ui`, styled by `ui/demo.css`.
     *
     * - **The demo panel**, on the right, under a title: the scene switcher (SetScenes), then the app's sections,
     *   which stay, then the scene's, which ClearScene removes. It scrolls when it is taller than the window.
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
         * @brief Sets the scene switcher, the panel's first app section, "Scenes": one button per name, in order. A
         * click on one calls `enter` with its index, the active one's too; the caller enters the scene and marks it
         * with SetActiveScene. Empty names remove the switcher.
         */
        void SetScenes(std::vector<std::string> names, std::function<void(std::size_t)> enter);
        /** @brief Marks the scene switcher's button `index` with the class `active`, and no other. */
        void SetActiveScene(std::size_t index);

        /**
         * @brief Sets the help: `heading` (the app and the scene), then `rows` in order. Logged as `Help: ...` lines
         * while the kit shows.
         */
        void SetHelp(std::string heading, std::vector<DemoKeyRow> rows);

      private:
        friend class DemoRow;
        friend class DemoTextRow;
        friend class DemoReadout;
        friend class DemoSection;

        /** @brief What a row is. */
        enum class RowKind : std::uint8_t
        {
            Text,
            Readout,
            Separator,
            /** @brief A switch row: its DemoSwitch makes its elements and keeps its control and its value in step. */
            Switch,
        };

        /** @brief A row as the kit keeps it, so it can be made again in a reloaded document. */
        struct Row
        {
            std::uint32_t id = 0;
            RowKind kind = RowKind::Text;
            /** @brief A text row's line, a readout's value or a separator's title. */
            std::string text;
            /** @brief A readout's label. */
            std::string label;
            /** @brief A pulled readout's value, called once a frame. */
            std::function<std::string()> pull;
            /** @brief A switch row's control; shared, so the listeners and the frame's sync outlive a moved row. */
            std::shared_ptr<Detail::DemoSwitch> control;
            /** @brief What the hint line shows while the row is hovered or has the focus. */
            std::string hint;
            bool shown = true;
            bool disabled = false;
            /** @brief The row's element: what SetShown and SetDisabled change. */
            Fenrir::ElementId element;
            /** @brief The element whose text is `text`: the row's own, or a readout's value. */
            Fenrir::ElementId textElement;
        };

        /** @brief A section as the kit keeps it. */
        struct Section
        {
            std::uint32_t id = 0;
            std::string title;
            bool scene = false;
            /** @brief The scene switcher, whose body holds the scene buttons rather than rows. */
            bool switcher = false;
            bool open = true;
            std::vector<Row> rows;
            Fenrir::ElementId element;
            Fenrir::ElementId body;
        };

        /** @brief Finds the document's containers again and makes every section, the title and the help in them. */
        void Build();
        DemoSection AddSection(std::string title, bool scene);
        /** @brief Makes `section`'s elements, at `index` among its container's children (UIRoot::End, the last). */
        void BuildSection(Section& section, std::size_t index = std::numeric_limits<std::size_t>::max());
        /** @brief Adds `row` under a new id to the section `section` names and makes it, or nothing when that is gone.
         */
        Row* AddRow(std::uint32_t section, Row row);
        void BuildRow(Row& row, Fenrir::ElementId body);
        /** @brief Changes `row`'s text, and its element's, only when it differs. */
        void SetRowText(Row& row, std::string_view text) const;
        void BuildSceneButtons(const Section& section);
        void ApplyActiveScene() const;
        /** @brief The build stage work: the pulled readouts' values. */
        void PullReadouts();
        /** @brief Adds a switch row to the section `section` names; an empty handle when that is gone. */
        DemoRow AddSwitch(std::uint32_t section, std::string label, std::shared_ptr<Detail::DemoSwitch> control,
                          std::string hint);
        /** @brief The build stage work: each switch row's control shows its bound value. */
        void SyncSwitches();
        /** @brief The build stage work: the hint line shows the hovered row's hint, or the focused row's. */
        void UpdateHint();
        /** @brief The hint of the row `element` is in, or empty. */
        [[nodiscard]] std::string_view HintOf(Fenrir::ElementId element) const;
        void BuildHelp();
        void ApplyOpen(const Section& section) const;
        void ApplyRow(const Row& row) const;
        void ApplyVisible() const;
        /** @brief Shows `element` or takes it out of the layout. */
        void Display(Fenrir::ElementId element, bool shown) const;
        void LogHelp() const;
        /** @brief The build stage work, too: F1 from the game's keyboard, which sees it while the kit has no focus. */
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
        /** @brief The scene switcher's names, what a click on one calls, its buttons and the marked one. */
        std::vector<std::string> m_sceneNames;
        std::function<void(std::size_t)> m_enterScene;
        std::vector<Fenrir::ElementId> m_sceneButtons;
        std::size_t m_activeScene = 0;
        std::uint32_t m_nextId = 1;
        /** @brief The document's containers, found again after a reload. */
        Fenrir::ElementId m_kit;
        Fenrir::ElementId m_panelTitle;
        Fenrir::ElementId m_appSections;
        Fenrir::ElementId m_sceneSections;
        Fenrir::ElementId m_help;
        Fenrir::ElementId m_helpBody;
        Fenrir::ElementId m_hint;
        /** @brief A hover or focus change since the hint line was last set. */
        bool m_hintDirty = true;
    };
} // namespace FenrirDemo
