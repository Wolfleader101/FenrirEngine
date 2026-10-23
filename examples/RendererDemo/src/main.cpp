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
                     RendererDemo::DebugDrawing()};
    // Every scene's edges are smoothed by 4x MSAA, resolved in each view's Output pass.
    config.msaa = 4;

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
