#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirUI/UI.hpp"
#include "UIDemo.hpp"

namespace UIDemo
{
    namespace
    {
        /**
         * @brief What installPackages hands the scaffold to keep: FenrirUI, and the WorldUI, Menu and Hud resources
         * over it, which go first, so their roots are destroyed while the UI is still there.
         */
        struct UIPackages
        {
            Fenrir::App* app = nullptr;
            Fenrir::UI ui;

            UIPackages(Fenrir::App& owner, Fenrir::UI installed) : app(&owner), ui(std::move(installed))
            {
            }
            UIPackages(const UIPackages&) = delete;
            UIPackages& operator=(const UIPackages&) = delete;
            UIPackages(UIPackages&&) = delete;
            UIPackages& operator=(UIPackages&&) = delete;
            ~UIPackages()
            {
                if (app->TryGetResource<WorldUI>() != nullptr)
                {
                    app->RemoveResource<WorldUI>();
                }
                if (app->TryGetResource<Menu>() != nullptr)
                {
                    app->RemoveResource<Menu>();
                }
                if (app->TryGetResource<Hud>() != nullptr)
                {
                    app->RemoveResource<Hud>();
                }
            }
        };

        /** @brief Registers SdfFamily: the font library's embedded face, in SDF mode (ADR-0062). */
        void RegisterSdfFamily(Fenrir::App& app)
        {
            auto& roots = app.GetResource<Fenrir::UIRoots>();
            const Fenrir::FontLibrary* fonts = roots.GetFonts();
            if (fonts == nullptr)
            {
                app.Logger()->Error("UIDemo: the UI has no font library, so the {} family is missing", SdfFamily);
                return;
            }
            Fenrir::UIFontFace face;
            face.family = SdfFamily;
            face.font = fonts->DefaultFont();
            face.rendering = Fenrir::FontRendering::Sdf;
            (void)roots.AddFontFace(face);
        }
    } // namespace

    FenrirDemo::DemoConfig MakeDemoConfig(HudConfig hud, MenuConfig menu)
    {
        FenrirDemo::DemoConfig config;
        config.appName = "UIDemo";
        config.scenes = {Room()};
        config.msaa = 4;
        // The room lists its switches in an ImGui window, as LightingDemo does.
        config.imguiOverlay = true;
        config.installPackages = [hud = std::move(hud),
                                  menu = std::move(menu)](Fenrir::App& app) -> std::shared_ptr<void> {
            auto packages = std::make_shared<UIPackages>(app, Fenrir::UI::Install(app));
            if (packages->ui.IsValid())
            {
                RegisterSdfFamily(app);
                app.AddResource<Hud>(app, hud);
                app.AddResource<Menu>(app, menu);
                app.AddResource<WorldUI>(app);
            }
            return packages;
        };
        return config;
    }
} // namespace UIDemo
