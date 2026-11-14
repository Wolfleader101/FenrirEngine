#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "DemoUI.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIImageTarget.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIDemo.hpp"

namespace UIDemo
{
    namespace
    {
        constexpr const char* MenuDocument = "ui/menu.ui";
        constexpr const char* SettingsDocument = "ui/settings.ui";
        constexpr const char* StylesheetFile = "ui/menu.css";
    } // namespace

    Menu::Menu(Fenrir::App& app, const MenuConfig& config) : m_app(app)
    {
        m_files = UIFileRoot(app.GetResource<Fenrir::AssetStore>(), StylesheetFile).generic_string();
        m_stylesheetPath = (std::filesystem::path(m_files) / StylesheetFile).generic_string();

        Fenrir::UIRootConfig rootConfig;
        rootConfig.layer = MenuLayer;
        rootConfig.inputPriority = MenuInputPriority;
        rootConfig.target = config.target;
        rootConfig.hotReload = true;
        m_root = &MakeRoot(std::move(rootConfig));
        app.Logger()->Info("UIDemo: the menu's stylesheet is {}; edit it while the demo runs to restyle the menu",
                           m_stylesheetPath);

        // The screen's root: the same document and stylesheet in an image, blending linear as an image root does by
        // default, since the screen's material reads it. Its document is there from the start.
        m_screenTarget = std::make_shared<Fenrir::UIImageTarget>(app, ScreenWidth, ScreenHeight);
        Fenrir::UIRootConfig screenConfig;
        screenConfig.target = m_screenTarget;
        screenConfig.hotReload = true;
        m_screen.root = &MakeRoot(std::move(screenConfig));
        m_screen.menu = Load(*m_screen.root, m_screen.root->GetRootElement(), MenuDocument);

        // Events sent before the menu are not its.
        (void)m_events.Read(app);
    }

    Menu::~Menu()
    {
        if (auto* roots = m_app.TryGetResource<Fenrir::UIRoots>(); roots != nullptr)
        {
            for (const Fenrir::UIRoot* root : {m_root, m_screen.root})
            {
                if (root != nullptr)
                {
                    roots->DestroyRoot(*root);
                }
            }
        }
    }

    Fenrir::AssetHandle<Fenrir::Image> Menu::ScreenImage() const
    {
        return m_screenTarget->GetImage();
    }

    Fenrir::UIRoot& Menu::MakeRoot(Fenrir::UIRootConfig config)
    {
        Fenrir::UIRoot& root = m_app.GetResource<Fenrir::UIRoots>().CreateRoot(std::move(config));
        const Fenrir::StylesheetId stylesheet = root.LoadStylesheet(m_stylesheetPath);
        // FenrirUI logs each problem as a warning; a menu missing a part is an error, so a smoke run fails on it.
        if (!stylesheet.IsValid())
        {
            m_app.Logger()->Error("UIDemo: the menu's stylesheet {} did not load", m_stylesheetPath);
        }
        else if (const Fenrir::Stylesheet* sheet = root.GetStylesheet(stylesheet);
                 sheet != nullptr && !sheet->diagnostics.empty())
        {
            m_app.Logger()->Error("UIDemo: the menu's stylesheet {} has {} problems, logged above", m_stylesheetPath,
                                  sheet->diagnostics.size());
        }
        return root;
    }

    Fenrir::ElementId Menu::Load(Fenrir::UIRoot& root, Fenrir::ElementId parent, const char* file)
    {
        const std::string path = (std::filesystem::path(m_files) / file).generic_string();
        const Fenrir::MarkupLoad load = root.LoadDocument(parent, path);
        if (!load.element.IsValid() || !load.errors.empty())
        {
            m_app.Logger()->Error("UIDemo: the menu's document {} loaded with {} problems, logged above", path,
                                  load.errors.size());
        }
        return load.element;
    }

    bool Menu::Holds(Fenrir::ElementId element) const
    {
        for (Fenrir::ElementId up = element; m_menu.IsValid() && up.IsValid(); up = m_root->GetParent(up))
        {
            if (up == m_menu)
            {
                return true;
            }
        }
        return false;
    }

    Fenrir::ElementId Menu::Find(const Fenrir::UIRoot& root, Fenrir::ElementId under, std::string_view id)
    {
        // Searched under the open menu, not the whole root: a menu closed a moment ago is still there in its outro,
        // with the same ids.
        if (!under.IsValid() || root.GetIdString(under) == id)
        {
            return under;
        }
        for (const Fenrir::ElementId child : root.GetChildren(under))
        {
            if (const Fenrir::ElementId found = Find(root, child, id); found.IsValid())
            {
                return found;
            }
        }
        return {};
    }

    Fenrir::ElementId Menu::Find(Fenrir::ElementId under, std::string_view id) const
    {
        return Find(*m_root, under, id);
    }

    void Menu::Open()
    {
        if (m_open)
        {
            return;
        }
        // A new document each time: its elements come in through :intro, and leave through :outro on Close.
        m_menu = Load(*m_root, m_root->GetRootElement(), MenuDocument);
        m_panel = {};
        m_open = true;
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
        m_root->Remove(m_menu);
        m_menu = {};
        m_panel = {};
        m_open = false;
    }

    void Menu::OpenSettings()
    {
        if (m_panel.IsValid() || !m_menu.IsValid())
        {
            return;
        }
        m_panel = Load(*m_root, m_menu, SettingsDocument);
        ShowSettings({m_root, m_menu, m_panel});
    }

    void Menu::CloseSettings()
    {
        if (!m_panel.IsValid())
        {
            return;
        }
        // The focus goes back to the button that opened the panel, so a gamepad is not left with none.
        for (Fenrir::ElementId up = m_root->GetFocused(); up.IsValid(); up = m_root->GetParent(up))
        {
            if (up == m_panel)
            {
                m_root->Focus(Find(m_menu, "settings"));
                break;
            }
        }
        m_root->Remove(m_panel);
        m_panel = {};
    }

    void Menu::Back()
    {
        if (m_panel.IsValid())
        {
            CloseSettings();
        }
        else
        {
            Close();
        }
    }

    void Menu::ShowSettings(const Copy& copy) const
    {
        const Fenrir::UIRoot& root = *copy.root;
        if (auto* volume = As<Fenrir::Slider>(root, Find(root, copy.panel, "volume")))
        {
            volume->SetValue(m_settings.volume);
        }
        if (auto* fov = As<Fenrir::Slider>(root, Find(root, copy.panel, "field-of-view")))
        {
            fov->SetValue(m_settings.fieldOfView);
        }
        const auto check = [&](const char* id, bool on) {
            if (auto* box = As<Fenrir::Checkbox>(root, Find(root, copy.panel, id)))
            {
                box->SetChecked(on);
            }
        };
        check("invert-look", m_settings.invertLook);
        check("show-hud", m_settings.showHud);
        check("virtual-cursor", m_settings.virtualCursor);
        if (auto* name = As<Fenrir::TextEntry>(root, Find(root, copy.panel, "player-name")))
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
                m_panel.IsValid() ? CloseSettings() : OpenSettings();
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
            // Hot reload made a document's elements again, sending this at its new first element: the menu's takes
            // its settings panel with it. Show the kept settings in the new controls.
            if (!m_open)
            {
                // A closed menu's document saved during its outro loads again: it is not wanted.
                root.Remove(event.target, Fenrir::RemoveMode::Immediate);
                break;
            }
            if (id == "menu")
            {
                m_menu = event.target;
                m_panel = {};
            }
            else if (id == "settings-panel")
            {
                m_panel = event.target;
                ShowSettings({m_root, m_menu, m_panel});
            }
            break;
        default:
            break;
        }
    }

    void Menu::OnScreenEvent(const Fenrir::UIEvent& event)
    {
        // The screen's root has no input router, and its controls change only from MirrorScreen, whose events are
        // not the menu's: a reloaded document is all that is new to it.
        if (event.kind != Fenrir::UIEventKind::DocumentReloaded)
        {
            return;
        }
        const std::string_view id = m_screen.root->GetIdString(event.target);
        if (id == "menu")
        {
            m_screen.menu = event.target;
            m_screen.panel = {};
        }
        else if (id == "settings-panel")
        {
            m_screen.panel = event.target;
        }
        // The new controls show the kept settings.
        m_screenShows.reset();
    }

    void Menu::MirrorScreen()
    {
        Fenrir::UIRoot& screen = *m_screen.root;
        // The settings panel is open on the screen while it is open in the live menu.
        if (m_panel.IsValid() && !m_screen.panel.IsValid())
        {
            m_screen.panel = Load(screen, m_screen.menu, SettingsDocument);
            m_screenShows.reset();
        }
        else if (!m_panel.IsValid() && m_screen.panel.IsValid())
        {
            screen.Remove(m_screen.panel);
            m_screen.panel = {};
        }
        if (m_screen.panel.IsValid() && m_screenShows != m_settings)
        {
            ShowSettings(m_screen);
            m_screenShows = m_settings;
        }

        // The focus is on the screen's element with the live focus's id, or on none.
        const Fenrir::ElementId focused = m_root->GetFocused();
        const Fenrir::ElementId mirrored =
            Holds(focused) ? Find(screen, m_screen.menu, m_root->GetIdString(focused)) : Fenrir::ElementId{};
        if (screen.GetFocused() != mirrored)
        {
            if (mirrored.IsValid())
            {
                screen.Focus(mirrored);
            }
            else
            {
                screen.Blur();
            }
        }
    }

    void Menu::Update()
    {
        // Escape reaches the game while none of the menu's elements has the focus, and so does Tab: it then gives
        // Resume the focus, as a gamepad's first direction does.
        const auto& keyboard = m_app.GetResource<Fenrir::Keyboard>();
        if (keyboard.JustPressed(Fenrir::Key::Escape))
        {
            m_open ? Back() : Open();
        }
        else if (m_open && keyboard.JustPressed(Fenrir::Key::Tab) && !m_root->GetFocused().IsValid())
        {
            m_root->Focus(Find(m_menu, "resume"));
        }
        for (const Fenrir::UIEvent& event : m_events.Read(m_app))
        {
            // A menu closed a moment ago is still there in its outro, hit-tested: what happens to it is not the open
            // menu's. A reloaded document's first element is new, so it is taken as it comes.
            if (event.root == m_root->GetId() &&
                (event.kind == Fenrir::UIEventKind::DocumentReloaded || Holds(event.target)))
            {
                OnEvent(event);
            }
            else if (event.root == m_screen.root->GetId())
            {
                OnScreenEvent(event);
            }
        }
        MirrorScreen();
    }
} // namespace UIDemo
