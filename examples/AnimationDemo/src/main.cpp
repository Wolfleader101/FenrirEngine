#include <memory>
#include <utility>

#include "AnimationScenes.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

    FenrirDemo::DemoConfig config;
    config.appName = "AnimationDemo";
    config.scenes = {AnimationDemo::Playback(), AnimationDemo::SkeletonScene(), AnimationDemo::SkinDiagnostics(),
                     AnimationDemo::Crowd()};
    config.msaa = 4;

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    // After the scaffold, which installs FenrirAssets that both need, so they end before it: Animation before the
    // Skeletons owner it was installed with. The renderer finds the skinning source Skeletons registers.
    Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);
    Fenrir::Animation animation = Fenrir::Animation::Install(app, skeletons);
    return demo.Run();
}
