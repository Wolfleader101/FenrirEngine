#include <filesystem>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIDemo.hpp"
#include "UIFiles.hpp"

namespace UIDemo
{
    namespace
    {
        constexpr const char* MenuDocument = "ui/menu.ui";
        constexpr const char* SettingsDocument = "ui/settings.ui";
        constexpr const char* StylesheetFile = "ui/menu.css";

        /** @brief `element`'s behaviour as a `T`, or null when it is stale or of another class. */
        template <typename T>
        T* As(const Fenrir::UIRoot& root, Fenrir::ElementId element)
        {
            return dynamic_cast<T*>(root.GetElement(element));
        }
    } // namespace

    Menu::Menu(Fenrir::App& app, const MenuConfig& config) : m_app(app)
    {
        Fenrir::UIRootConfig rootConfig;
        rootConfig.layer = MenuLayer;
        rootConfig.inputPriority = MenuInputPriority;
        rootConfig.target = config.target;
        rootConfig.hotReload = true;
        m_root = &app.GetResource<Fenrir::UIRoots>().CreateRoot(rootConfig);

        m_files = UIFileRoot(app.GetResource<Fenrir::AssetStore>(), StylesheetFile).generic_string();
        m_stylesheetPath = (std::filesystem::path(m_files) / StylesheetFile).generic_string();
        m_stylesheet = m_root->LoadStylesheet(m_stylesheetPath);
        // FenrirUI logs each problem as a warning; a menu missing a part is an error, so a smoke run fails on it.
        if (!m_stylesheet.IsValid())
        {
            app.Logger()->Error("UIDemo: the menu's stylesheet {} did not load", m_stylesheetPath);
        }
        else if (const Fenrir::Stylesheet* sheet = m_root->GetStylesheet(m_stylesheet);
                 sheet != nullptr && !sheet->diagnostics.empty())
        {
            app.Logger()->Error("UIDemo: the menu's stylesheet {} has {} problems, logged above", m_stylesheetPath,
                                sheet->diagnostics.size());
        }
        app.Logger()->Info("UIDemo: the menu's stylesheet is {}; edit it while the demo runs to restyle the menu",
                           m_stylesheetPath);
        // Events sent before the menu are not its.
        (void)m_events.Read(app);
    }

    Menu::~Menu()
    {
        if (auto* roots = m_app.TryGetResource<Fenrir::UIRoots>(); roots != nullptr && m_root != nullptr)
        {
            roots->DestroyRoot(*m_root);
        }
    }

    void Menu::Load(Fenrir::ElementId parent, const char* file)
    {
        const std::string path = (std::filesystem::path(m_files) / file).generic_string();
        const Fenrir::MarkupLoad load = m_root->LoadDocument(parent, path);
        if (!load.element.IsValid() || !load.errors.empty())
        {
            m_app.Logger()->Error("UIDemo: the menu's document {} loaded with {} problems, logged above", path,
                                  load.errors.size());
        }
    }

    void Menu::Open()
    {
        if (m_open)
        {
            return;
        }
        // A new document each time: its elements come in through :intro, and leave through :outro on Close.
        Load(m_root->GetRootElement(), MenuDocument);
        m_open = true;
        m_settingsOpen = false;
        m_root->SetVirtualCursor(m_settings.virtualCursor);
    }

    void Menu::Close()
    {
        if (!m_open)
        {
            return;
        }
        m_root->Blur();
        m_root->SetVirtualCursor(false);
        // Its settings panel is under it, so it goes too.
        m_root->Remove(m_root->FindById("menu"));
        m_open = false;
        m_settingsOpen = false;
    }

    void Menu::OpenSettings()
    {
        const Fenrir::ElementId menu = m_root->FindById("menu");
        if (m_settingsOpen || !menu.IsValid())
        {
            return;
        }
        Load(menu, SettingsDocument);
        m_settingsOpen = true;
        ShowSettings();
    }

    void Menu::CloseSettings()
    {
        if (!m_settingsOpen)
        {
            return;
        }
        // The focus goes back to the button that opened the panel, so a gamepad is not left with none.
        const Fenrir::ElementId panel = m_root->FindById("settings-panel");
        const Fenrir::ElementId focused = m_root->GetFocused();
        for (Fenrir::ElementId up = focused; up.IsValid(); up = m_root->GetParent(up))
        {
            if (up == panel)
            {
                m_root->Focus(m_root->FindById("settings"));
                break;
            }
        }
        m_root->Remove(panel);
        m_settingsOpen = false;
    }

    void Menu::Back()
    {
        if (m_settingsOpen)
        {
            CloseSettings();
        }
        else
        {
            Close();
        }
    }

    void Menu::ShowSettings()
    {
        Fenrir::UIRoot& root = *m_root;
        if (auto* volume = As<Fenrir::Slider>(root, root.FindById("volume")))
        {
            volume->SetValue(m_settings.volume);
        }
        if (auto* fov = As<Fenrir::Slider>(root, root.FindById("field-of-view")))
        {
            fov->SetValue(m_settings.fieldOfView);
        }
        const auto check = [&](const char* id, bool on) {
            if (auto* box = As<Fenrir::Checkbox>(root, root.FindById(id)))
            {
                box->SetChecked(on);
            }
        };
        check("invert-look", m_settings.invertLook);
        check("show-hud", m_settings.showHud);
        check("virtual-cursor", m_settings.virtualCursor);
        if (auto* name = As<Fenrir::TextEntry>(root, root.FindById("player-name")))
        {
            name->SetValue(m_settings.playerName);
        }
    }

    void Menu::OnEvent(const Fenrir::UIEvent& event)
    {
        Fenrir::UIRoot& root = *m_root;
        const std::string_view id = root.GetIdString(event.target);
        switch (event.kind)
        {
        case Fenrir::UIEventKind::Click:
            if (id == "resume")
            {
                Close();
            }
            else if (id == "settings")
            {
                m_settingsOpen ? CloseSettings() : OpenSettings();
            }
            else if (id == "back")
            {
                CloseSettings();
            }
            else if (id == "quit")
            {
                m_app.Stop();
            }
            break;
        case Fenrir::UIEventKind::Change:
            if (id == "volume")
            {
                m_settings.volume = event.value;
            }
            else if (id == "field-of-view")
            {
                m_settings.fieldOfView = event.value;
            }
            else if (auto* box = As<Fenrir::Checkbox>(root, event.target))
            {
                if (id == "invert-look")
                {
                    m_settings.invertLook = box->IsChecked();
                }
                else if (id == "show-hud")
                {
                    m_settings.showHud = box->IsChecked();
                }
                else if (id == "virtual-cursor")
                {
                    m_settings.virtualCursor = box->IsChecked();
                    root.SetVirtualCursor(m_settings.virtualCursor);
                }
            }
            else if (auto* name = As<Fenrir::TextEntry>(root, event.target); name != nullptr && id == "player-name")
            {
                m_settings.playerName = std::string(name->GetValue());
            }
            break;
        case Fenrir::UIEventKind::Cancel:
            // B on a gamepad steps back.
            Back();
            break;
        case Fenrir::UIEventKind::KeyPress:
            // While one of the menu's elements has the focus, the root has the keyboard, so Escape comes here. A text
            // entry's Escape only takes its focus away.
            if (event.key == Fenrir::Key::Escape && event.value == 0.0f &&
                As<Fenrir::TextEntry>(root, event.target) == nullptr)
            {
                Back();
            }
            break;
        case Fenrir::UIEventKind::DocumentReloaded:
            // Hot reload made a document's elements again: the menu's takes its settings panel with it. Show the kept
            // settings in the new controls.
            m_settingsOpen = root.FindById("settings-panel").IsValid();
            ShowSettings();
            break;
        default:
            break;
        }
    }

    void Menu::Update()
    {
        // Escape reaches the game while none of the menu's elements has the focus.
        if (m_app.GetResource<Fenrir::Keyboard>().JustPressed(Fenrir::Key::Escape))
        {
            m_open ? Back() : Open();
        }
        for (const Fenrir::UIEvent& event : m_events.Read(m_app))
        {
            if (event.root == m_root->GetId())
            {
                OnEvent(event);
            }
        }
    }
} // namespace UIDemo
