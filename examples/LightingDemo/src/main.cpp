#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "LightingScenes.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

    FenrirDemo::DemoConfig config;
    config.appName = "LightingDemo";
    config.scenes = {LightingDemo::MaterialGrid(), LightingDemo::ShowcaseModels()};
    config.msaa = 4;
    // The Lighting window, in every scene.
    config.imguiOverlay = true;

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
