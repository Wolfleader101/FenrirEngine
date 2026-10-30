#include <memory>
#include <utility>

#include "AnimationScenes.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

#ifdef ANIMATION_DEMO_PHYSICS
#include "FenrirPhysics/Physics.hpp"
#endif

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

#ifdef ANIMATION_DEMO_PHYSICS
    // For scene 5's shield and boxes. Before the scaffold, so it ends after it: the scaffold destroys the active scene,
    // its physics world with it, while physics is still there.
    Fenrir::Physics physics = Fenrir::Physics::Install(app);
#endif

    FenrirDemo::DemoConfig config;
    config.appName = "AnimationDemo";
    config.scenes = {AnimationDemo::Playback(), AnimationDemo::SkeletonScene(),      AnimationDemo::SkinDiagnostics(),
                     AnimationDemo::Blending(), AnimationDemo::BoneAttachments(app), AnimationDemo::GoingLimp(),
                     AnimationDemo::Crowd()};
    config.msaa = 4;

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    // After the scaffold, which installs FenrirAssets that both need, so they end before it: Animation before the
    // Skeletons owner it was installed with. The renderer finds the skinning source Skeletons registers.
    Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);
    Fenrir::Animation animation = Fenrir::Animation::Install(app, skeletons);
    return demo.Run();
}
