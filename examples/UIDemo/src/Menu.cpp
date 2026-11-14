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
        m_live.root = &MakeRoot(std::move(rootConfig));
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
            for (const Fenrir::UIRoot* root : {m_live.root, m_screen.root})
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
        for (Fenrir::ElementId up = element; m_live.menu.IsValid() && up.IsValid(); up = m_live.root->GetParent(up))
        {
            if (up == m_live.menu)
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

    void Menu::Open()
    {
        if (m_open)
        {
            return;
        }
        // A new document each time: its elements come in through :intro, and leave through :outro on Close.
        m_live.menu = Load(*m_live.root, m_live.root->GetRootElement(), MenuDocument);
        m_live.panel = {};
        m_open = true;
        m_live.root->SetVirtualCursor(m_settings.virtualCursor);
    }

    void Menu::Close()
    {
        if (!m_open)
        {
            return;
        }
        m_live.root->Blur();
        m_live.root->SetVirtualCursor(false);
        // Its settings panel is under it, so it goes too.
        m_live.root->Remove(m_live.menu);
        m_live.menu = {};
        m_live.panel = {};
        m_open = false;
    }

    void Menu::OpenSettings()
    {
        if (m_live.panel.IsValid() || !m_live.menu.IsValid())
        {
            return;
        }
        m_live.panel = Load(*m_live.root, m_live.menu, SettingsDocument);
        ShowSettings(m_live);
    }

    void Menu::CloseSettings()
    {
        if (!m_live.panel.IsValid())
        {
            return;
        }
        // The focus goes back to the button that opened the panel, so a gamepad is not left with none.
        for (Fenrir::ElementId up = m_live.root->GetFocused(); up.IsValid(); up = m_live.root->GetParent(up))
        {
            if (up == m_live.panel)
            {
                m_live.root->Focus(Find(*m_live.root, m_live.menu, "settings"));
                break;
            }
        }
        m_live.root->Remove(m_live.panel);
        m_live.panel = {};
    }

    void Menu::Back()
    {
        if (m_live.panel.IsValid())
        {
            CloseSettings();
        }
        else
        {
            Close();
        }
    }

    void Menu::ShowSettings(const MenuInstance& instance) const
    {
        const Fenrir::UIRoot& root = *instance.root;
        if (auto* volume = As<Fenrir::Slider>(root, Find(root, instance.panel, "volume")))
        {
            volume->SetValue(m_settings.volume);
        }
        if (auto* fov = As<Fenrir::Slider>(root, Find(root, instance.panel, "field-of-view")))
        {
            fov->SetValue(m_settings.fieldOfView);
        }
        const auto check = [&](const char* id, bool on) {
            if (auto* box = As<Fenrir::Checkbox>(root, Find(root, instance.panel, id)))
            {
                box->SetChecked(on);
            }
        };
        check("invert-look", m_settings.invertLook);
        check("show-hud", m_settings.showHud);
        check("virtual-cursor", m_settings.virtualCursor);
        if (auto* name = As<Fenrir::TextEntry>(root, Find(root, instance.panel, "player-name")))
        {
            name->SetValue(m_settings.playerName);
        }
    }

    void Menu::OnEvent(const Fenrir::UIEvent& event)
    {
        Fenrir::UIRoot& root = *m_live.root;
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
                m_live.panel.IsValid() ? CloseSettings() : OpenSettings();
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
            if (TakeReloaded(m_live, event.target))
            {
                ShowSettings(m_live);
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
        TakeReloaded(m_screen, event.target);
        // The new controls start from the document's values, not the kept settings, so they are written again.
        m_screenSettings.reset();
    }

    bool Menu::TakeReloaded(MenuInstance& instance, Fenrir::ElementId first)
    {
        const std::string_view id = instance.root->GetIdString(first);
        if (id == "menu")
        {
            // The menu's document took its settings panel with it.
            instance.menu = first;
            instance.panel = {};
        }
        else if (id == "settings-panel")
        {
            instance.panel = first;
            return true;
        }
        return false;
    }

    void Menu::MirrorScreen()
    {
        Fenrir::UIRoot& screenRoot = *m_screen.root;
        // A panel is loaded and removed rather than hidden, so the screen's comes and goes through :intro and
        // :outro as the live one does.
        if (m_live.panel.IsValid() && !m_screen.panel.IsValid())
        {
            m_screen.panel = Load(screenRoot, m_screen.menu, SettingsDocument);
            m_screenSettings.reset();
        }
        else if (!m_live.panel.IsValid() && m_screen.panel.IsValid())
        {
            screenRoot.Remove(m_screen.panel);
            m_screen.panel = {};
        }
        if (m_screen.panel.IsValid() && m_screenSettings != m_settings)
        {
            ShowSettings(m_screen);
            m_screenSettings = m_settings;
        }

        // The live focus is found by id because the two roots' elements have ids of their own; it shows the focus
        // ring on the screen, which takes no input to move a focus of its own.
        const Fenrir::ElementId focused = m_live.root->GetFocused();
        const Fenrir::ElementId mirrored =
            Holds(focused) ? Find(screenRoot, m_screen.menu, m_live.root->GetIdString(focused)) : Fenrir::ElementId{};
        if (screenRoot.GetFocused() != mirrored)
        {
            if (mirrored.IsValid())
            {
                screenRoot.Focus(mirrored);
            }
            else
            {
                screenRoot.Blur();
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
        else if (m_open && keyboard.JustPressed(Fenrir::Key::Tab) && !m_live.root->GetFocused().IsValid())
        {
            m_live.root->Focus(Find(*m_live.root, m_live.menu, "resume"));
        }
        for (const Fenrir::UIEvent& event : m_events.Read(m_app))
        {
            // A menu closed a moment ago is still there in its outro, hit-tested: what happens to it is not the open
            // menu's. A reloaded document's first element is new, so it is taken as it comes.
            if (event.root == m_live.root->GetId() &&
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
