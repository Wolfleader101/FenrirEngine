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
    config.scenes = {LightingDemo::MaterialGrid(), LightingDemo::ShowcaseModels(), LightingDemo::ManyLights(),
                     LightingDemo::Showroom(),     LightingDemo::AsteroidRing(),   LightingDemo::ForestClearing(),
                     LightingDemo::NeonCity(),     LightingDemo::Sponza()};
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    // The lighting spec from C#: Many lights' twin, made and run by scripts/ManyLights.cs.
    config.scenes.push_back(LightingDemo::ManyLightsCSharp());
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
#endif
    config.msaa = 4;
    // The Lighting window, in every scene.
    config.imguiOverlay = true;

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
