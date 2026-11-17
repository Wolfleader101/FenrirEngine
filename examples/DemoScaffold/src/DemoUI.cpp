#include "FenrirDemoScaffold/DemoUI.hpp"

#include <algorithm>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace FenrirDemo
{
    namespace
    {
        constexpr const char* DocumentFile = "ui/demo.ui";
        constexpr const char* StylesheetFile = "ui/demo.css";

        // The build stage order of the kit's work: early, so a scene's rows pushed later in the stage win.
        constexpr std::int32_t BuildStageOrder = -100;

        /** @brief Logs an error for a sheet of the kit's that did not load or loaded with problems. */
        void ReportStylesheet(Fenrir::App& app, const Fenrir::UIRoot& root, Fenrir::StylesheetId sheet,
                              const std::string& path)
        {
            if (!sheet.IsValid())
            {
                app.Logger()->Error("The demo UI kit's stylesheet {} did not load", path);
            }
            else if (const Fenrir::Stylesheet* loaded = root.GetStylesheet(sheet);
                     loaded != nullptr && !loaded->diagnostics.empty())
            {
                app.Logger()->Error("The demo UI kit's stylesheet {} has {} problems, logged above", path,
                                    loaded->diagnostics.size());
            }
        }
    } // namespace

    // Rows and sections.

    void DemoRow::SetShown(bool shown) const
    {
        if (DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr; row != nullptr && row->shown != shown)
        {
            row->shown = shown;
            m_ui->ApplyRow(*row);
        }
    }

    void DemoRow::SetDisabled(bool disabled) const
    {
        if (DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr;
            row != nullptr && row->disabled != disabled)
        {
            row->disabled = disabled;
            m_ui->ApplyRow(*row);
        }
    }

    bool DemoRow::IsValid() const
    {
        return m_ui != nullptr && m_ui->FindRow(m_id) != nullptr;
    }

    void DemoTextRow::Set(std::string_view line) const
    {
        DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr;
        if (row == nullptr || row->text == line)
        {
            return;
        }
        row->text = line;
        m_ui->Root().SetText(row->element, row->text);
    }

    DemoTextRow DemoSection::Text(std::string line) const
    {
        DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
        if (section == nullptr)
        {
            return {};
        }
        DemoUI::Row& row = section->rows.emplace_back();
        row.id = m_ui->m_nextId++;
        row.text = std::move(line);
        m_ui->BuildRow(row, section->body);
        return DemoTextRow(m_ui, row.id);
    }

    void DemoSection::SetOpen(bool open) const
    {
        if (DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
            section != nullptr && section->open != open)
        {
            section->open = open;
            m_ui->ApplyOpen(*section);
        }
    }

    bool DemoSection::IsOpen() const
    {
        const DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
        return section != nullptr && section->open;
    }

    bool DemoSection::IsValid() const
    {
        return m_ui != nullptr && m_ui->FindSection(m_id) != nullptr;
    }

    Fenrir::ElementId DemoSection::Element() const
    {
        const DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
        return section != nullptr ? section->element : Fenrir::ElementId{};
    }

    // The kit.

    DemoUI::DemoUI(Fenrir::App& app, DemoUIConfig config) : m_app(app), m_config(std::move(config))
    {
        auto& roots = app.GetResource<Fenrir::UIRoots>();
        Fenrir::UIRootConfig rootConfig;
        rootConfig.layer = DemoUILayer;
        rootConfig.inputPriority = DemoUIRouterPriority;
        rootConfig.target = m_config.target;
        rootConfig.hotReload = m_config.hotReload;
        // A pad always drives the scene: the kit is for the mouse and the keyboard.
        rootConfig.gamepadNavigation = false;
        m_root = &roots.CreateRoot(rootConfig);

        const std::filesystem::path& files = m_config.folder;
        m_stylesheetPath = (files / StylesheetFile).generic_string();
        ReportStylesheet(app, *m_root, m_root->LoadStylesheet(m_stylesheetPath), m_stylesheetPath);

        // F1 while a control of the kit has the focus: the root has the keyboard then, so the game's keyboard never
        // sees it.
        m_root->AddListener(m_root->GetRootElement(), Fenrir::UIEventKind::KeyPress,
                            [this](Fenrir::UIListenerContext& context) {
                                // A held key repeats its press; F1 toggles once a press.
                                if (context.GetEvent().value == 0.0f)
                                {
                                    OnKey(context.GetEvent().key);
                                }
                            });
        // A reloaded demo.ui makes the kit's elements again under new ids.
        m_root->AddListener(m_root->GetRootElement(), Fenrir::UIEventKind::DocumentReloaded,
                            [this](Fenrir::UIListenerContext&) { Build(); });
        m_buildStage = roots.AddBuildStage(BuildStageOrder, [this] { PollToggleKey(); });

        const std::string document = (files / DocumentFile).generic_string();
        const Fenrir::MarkupLoad load = m_root->LoadDocument(m_root->GetRootElement(), document);
        if (!load.element.IsValid() || !load.errors.empty())
        {
            app.Logger()->Error("The demo UI kit's document {} loaded with {} problems, logged above", document,
                                load.errors.size());
        }
        Build();
    }

    DemoUI::~DemoUI()
    {
        if (auto* roots = m_app.TryGetResource<Fenrir::UIRoots>(); roots != nullptr)
        {
            (void)roots->RemoveBuildStage(m_buildStage);
            if (m_root != nullptr)
            {
                (void)roots->DestroyRoot(*m_root);
            }
        }
    }

    Fenrir::UIRoot& DemoUI::Root() const noexcept
    {
        return *m_root;
    }

    void DemoUI::SetVisible(bool visible)
    {
        if (m_visible == visible)
        {
            return;
        }
        m_visible = visible;
        ApplyVisible();
        if (!visible)
        {
            // A hidden kit keeps no keys from the scene.
            m_root->Blur();
        }
        else
        {
            LogHelp();
        }
    }

    Fenrir::StylesheetId DemoUI::AddStylesheet(const std::string& path)
    {
        const Fenrir::StylesheetId sheet = m_root->LoadStylesheet(path);
        ReportStylesheet(m_app, *m_root, sheet, path);
        return sheet;
    }

    void DemoUI::SetTitle(std::string title)
    {
        m_title = std::move(title);
        m_root->SetText(m_panelTitle, m_title);
    }

    DemoSection DemoUI::AppSection(std::string title)
    {
        return AddSection(std::move(title), false);
    }

    DemoSection DemoUI::SceneSection(std::string title)
    {
        return AddSection(std::move(title), true);
    }

    void DemoUI::ClearScene()
    {
        for (const Section& section : m_sections)
        {
            if (section.scene)
            {
                (void)m_root->Remove(section.element, Fenrir::RemoveMode::Immediate);
            }
        }
        std::erase_if(m_sections, [](const Section& section) { return section.scene; });
    }

    void DemoUI::SetHelp(std::string heading, std::vector<DemoKeyRow> rows)
    {
        m_helpHeading = std::move(heading);
        m_helpRows = std::move(rows);
        BuildHelp();
        if (m_visible)
        {
            LogHelp();
        }
    }

    void DemoUI::Build()
    {
        m_kit = m_root->FindById("demo-kit");
        m_panelTitle = m_root->FindById("demo-panel-title");
        m_appSections = m_root->FindById("demo-app-sections");
        m_sceneSections = m_root->FindById("demo-scene-sections");
        m_help = m_root->FindById("demo-help");
        if (!m_kit.IsValid() || !m_appSections.IsValid() || !m_sceneSections.IsValid() || !m_help.IsValid())
        {
            m_app.Logger()->Error("The demo UI kit's document lacks #demo-kit, #demo-app-sections, "
                                  "#demo-scene-sections or #demo-help");
        }
        m_root->SetText(m_panelTitle, m_title);
        for (Section& section : m_sections)
        {
            BuildSection(section);
        }
        BuildHelp();
        ApplyVisible();
    }

    DemoSection DemoUI::AddSection(std::string title, bool scene)
    {
        Section& section = m_sections.emplace_back();
        section.id = m_nextId++;
        section.title = std::move(title);
        section.scene = scene;
        BuildSection(section);
        return DemoSection(this, section.id);
    }

    void DemoUI::BuildSection(Section& section)
    {
        Fenrir::UIRoot& root = *m_root;
        section.element = root.Create(section.scene ? m_sceneSections : m_appSections, "panel");
        root.AddClass(section.element, "demo-section");
        const Fenrir::ElementId title = root.Create(section.element, "button");
        root.AddClass(title, "demo-section-title");
        root.SetText(title, section.title);
        const DemoSection handle(this, section.id);
        root.AddListener(title, Fenrir::UIEventKind::Click,
                         [handle](Fenrir::UIListenerContext&) { handle.SetOpen(!handle.IsOpen()); });
        section.body = root.Create(section.element, "panel");
        root.AddClass(section.body, "demo-section-body");
        for (Row& row : section.rows)
        {
            BuildRow(row, section.body);
        }
        ApplyOpen(section);
    }

    void DemoUI::BuildRow(Row& row, Fenrir::ElementId body)
    {
        Fenrir::UIRoot& root = *m_root;
        row.element = root.Create(body, "label");
        root.AddClass(row.element, "demo-text");
        root.SetText(row.element, row.text);
        ApplyRow(row);
    }

    void DemoUI::BuildHelp()
    {
        Fenrir::UIRoot& root = *m_root;
        if (!m_help.IsValid())
        {
            return;
        }
        // The help is made whole each time it changes: a scene's entry, or a reloaded document.
        for (const Fenrir::ElementId child :
             std::vector<Fenrir::ElementId>(root.GetChildren(m_help).begin(), root.GetChildren(m_help).end()))
        {
            (void)root.Remove(child, Fenrir::RemoveMode::Immediate);
        }
        const Fenrir::ElementId title = root.Create(m_help, "button");
        root.AddClass(title, "demo-help-title");
        root.SetText(title, "Help");
        root.AddListener(title, Fenrir::UIEventKind::Click, [this](Fenrir::UIListenerContext&) {
            m_helpOpen = !m_helpOpen;
            Display(m_helpBody, m_helpOpen);
        });
        m_helpBody = root.Create(m_help, "panel");
        root.AddClass(m_helpBody, "demo-section-body");
        const Fenrir::ElementId heading = root.Create(m_helpBody, "label");
        root.AddClass(heading, "demo-help-heading");
        root.SetText(heading, m_helpHeading);
        for (const DemoKeyRow& keyRow : m_helpRows)
        {
            const Fenrir::ElementId row = root.Create(m_helpBody, "panel");
            root.AddClass(row, "demo-key-row");
            const Fenrir::ElementId keys = root.Create(row, "label");
            root.AddClass(keys, "demo-keys");
            root.SetText(keys, keyRow.keys);
            const Fenrir::ElementId what = root.Create(row, "label");
            root.AddClass(what, "demo-what");
            root.SetText(what, keyRow.what);
        }
        Display(m_helpBody, m_helpOpen);
    }

    void DemoUI::ApplyOpen(const Section& section) const
    {
        Display(section.body, section.open);
        if (section.open)
        {
            m_root->RemoveClass(section.element, "demo-closed");
        }
        else
        {
            m_root->AddClass(section.element, "demo-closed");
        }
    }

    void DemoUI::ApplyRow(const Row& row) const
    {
        Display(row.element, row.shown);
        if (row.disabled)
        {
            m_root->AddClass(row.element, "demo-disabled");
        }
        else
        {
            m_root->RemoveClass(row.element, "demo-disabled");
        }
    }

    void DemoUI::ApplyVisible() const
    {
        Display(m_kit, m_visible);
    }

    void DemoUI::Display(Fenrir::ElementId element, bool shown) const
    {
        // Inline, so a stylesheet's edit never shows what the kit hid; hidden by display, an element is neither drawn
        // nor hit-tested, and takes no room.
        (void)m_root->SetStyleProperty(element, "display", shown ? "" : "none");
    }

    void DemoUI::LogHelp() const
    {
        if (m_helpHeading.empty() && m_helpRows.empty())
        {
            return;
        }
        Fenrir::ILogger& log = *m_app.Logger();
        log.Info("Help: {}", m_helpHeading);
        for (const DemoKeyRow& row : m_helpRows)
        {
            if (row.keys.empty())
            {
                log.Info("Help: {}", row.what);
            }
            else
            {
                log.Info("Help: {}: {}", row.keys, row.what);
            }
        }
    }

    void DemoUI::PollToggleKey()
    {
        // While the kit has the focus the root takes the keys, and OnKey sees F1 instead.
        if (const auto* keyboard = m_app.TryGetResource<Fenrir::Keyboard>();
            keyboard != nullptr && keyboard->JustPressed(Fenrir::Key::F1))
        {
            SetVisible(!m_visible);
        }
    }

    void DemoUI::OnKey(Fenrir::Key key)
    {
        if (key != Fenrir::Key::F1)
        {
            return;
        }
        // F1 is a character of no text, but a text entry's keys are all its own.
        if (dynamic_cast<const Fenrir::TextEntry*>(m_root->GetElement(m_root->GetFocused())) != nullptr)
        {
            return;
        }
        SetVisible(!m_visible);
    }

    DemoUI::Section* DemoUI::FindSection(std::uint32_t id)
    {
        const auto found = std::find_if(m_sections.begin(), m_sections.end(),
                                        [id](const Section& section) { return section.id == id; });
        return found != m_sections.end() ? &*found : nullptr;
    }

    DemoUI::Row* DemoUI::FindRow(std::uint32_t id)
    {
        for (Section& section : m_sections)
        {
            for (Row& row : section.rows)
            {
                if (row.id == id)
                {
                    return &row;
                }
            }
        }
        return nullptr;
    }
} // namespace FenrirDemo
