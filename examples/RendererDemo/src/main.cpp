#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "RendererScenes.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

    FenrirDemo::DemoConfig config;
    config.appName = "RendererDemo";
    config.scenes = {RendererDemo::MeshesAndMaterials(), RendererDemo::CamerasAndTargets(), RendererDemo::Sky(),
                     RendererDemo::DebugDrawing(), RendererDemo::OverlayScene()};
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    // The C# twins, run by the app's scripts.
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
    config.scenes.push_back(RendererDemo::MeshesAndMaterialsCSharp());
#endif
    // Every scene's edges are smoothed by 4x MSAA, resolved in each view's Output pass.
    config.msaa = 4;
    // ImGui over every scene; Overlay is the scene that makes an ImGui window.
    config.imguiOverlay = true;

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    RendererDemo::AddCSharpShaders(app);
#endif
    return demo.Run();
}
