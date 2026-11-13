#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirUI/UI.hpp"
#include "UIDemo.hpp"

namespace UIDemo
{
    namespace
    {
        /**
         * @brief What installPackages hands the scaffold to keep: FenrirUI, and the Hud resource over it, which goes
         * first, so its root is destroyed while the UI is still there.
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
                if (app->TryGetResource<Hud>() != nullptr)
                {
                    app->RemoveResource<Hud>();
                }
            }
        };
    } // namespace

    FenrirDemo::DemoConfig MakeDemoConfig(HudConfig hud)
    {
        FenrirDemo::DemoConfig config;
        config.appName = "UIDemo";
        config.scenes = {Room()};
        config.msaa = 4;
        // The switches' window.
        config.imguiOverlay = true;
        config.installPackages = [hud = std::move(hud)](Fenrir::App& app) -> std::shared_ptr<void> {
            auto packages = std::make_shared<UIPackages>(app, Fenrir::UI::Install(app));
            if (packages->ui.IsValid())
            {
                app.AddResource<Hud>(app, hud);
            }
            return packages;
        };
        return config;
    }
} // namespace UIDemo
