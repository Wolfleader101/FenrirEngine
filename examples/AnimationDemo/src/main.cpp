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

#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
#include "FenrirAnimationScripting/AnimationScripting.hpp"
#include "FenrirSkeletonScripting/SkeletonScripting.hpp"
#endif

namespace
{
    // FenrirSkeleton and FenrirAnimation, which need the scaffold's FenrirAssets and come before Scripting, so a
    // script's pose stage systems find the pose pipeline when it loads. They end in reverse: Animation before the
    // Skeletons owner it was installed with. The renderer finds the skinning source Skeletons registers.
    struct AnimationPackages
    {
        explicit AnimationPackages(Fenrir::App& app)
            : skeletons(Fenrir::Skeletons::Install(app)), animation(Fenrir::Animation::Install(app, skeletons))
        {
        }

        Fenrir::Skeletons skeletons;
        Fenrir::Animation animation;
    };
} // namespace

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
    // The scaffold keeps them, after its own packages and before Scripting.
    config.installPackages = [](Fenrir::App& installing) -> std::shared_ptr<void> {
        return std::make_shared<AnimationPackages>(installing);
    };
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    // Scenes 8 and 9, the C# twins of Playback and Bone attachments, through the skeleton and animation extensions.
    config.scenes.push_back(AnimationDemo::PlaybackCSharp());
    config.scenes.push_back(AnimationDemo::BoneAttachmentsCSharp());
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
    config.scriptingExtensions = {Fenrir::Skeletons::ScriptingExtension(), Fenrir::Animation::ScriptingExtension()};
#endif

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
