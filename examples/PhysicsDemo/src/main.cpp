#include <memory>
#include <utility>

#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "PhysicsScenes.hpp"
#include "SceneLooks.hpp"

#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#endif

namespace
{
    // SPIKE (ticket 55): FenrirSkeleton, FenrirAnimation and the ragdoll glue for the Ragdolls (spike) scene, which
    // need the scaffold's FenrirAssets. They end in reverse: the glue, then Animation, then the Skeletons owner.
    struct RagdollPackages
    {
        explicit RagdollPackages(Fenrir::App& app)
            : skeletons(Fenrir::Skeletons::Install(app)), animation(Fenrir::Animation::Install(app, skeletons)),
              ragdolls(Fenrir::Ragdolls::Install(app))
        {
        }

        Fenrir::Skeletons skeletons;
        Fenrir::Animation animation;
        Fenrir::Ragdolls ragdolls;
    };
} // namespace

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

    // Before the scaffold, so it ends after it: the scaffold destroys the active scene, its physics world with it, and
    // ends scripting, which is given physics's extension, while physics is still there.
    Fenrir::Physics physics = Fenrir::Physics::Install(app);

    FenrirDemo::DemoConfig config;
    config.appName = "PhysicsDemo";
    config.appKeyHelp = "cycle the physics debug lines: off, colliders, contacts, velocities, bounds, joints, "
                        "characters, vehicles, all.";
    config.scenes.push_back(PhysicsDemo::Playground(app, physics));
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    config.scenes.push_back(PhysicsDemo::PlaygroundCSharp());
#endif
    config.scenes.push_back(PhysicsDemo::Joints(app));
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    config.scenes.push_back(PhysicsDemo::JointsCSharp());
#endif
    config.scenes.push_back(PhysicsDemo::Character(app, physics));
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    config.scenes.push_back(PhysicsDemo::CharacterCSharp());
#endif
    config.scenes.push_back(PhysicsDemo::Vehicles(app));
    config.scenes.push_back(PhysicsDemo::Motorcycle(app));
    config.scenes.push_back(PhysicsDemo::Tank(app));
    config.scenes.push_back(PhysicsDemo::Ragdolls(app));
    config.installPackages = [](Fenrir::App& installing) -> std::shared_ptr<void> {
        return std::make_shared<RagdollPackages>(installing);
    };
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    config.scenes.push_back(PhysicsDemo::VehiclesCSharp());
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
    // Physics from C#: the physics components, PhysicsWorld's functions and the Fenrir.Physics contract.
    config.scriptingExtensions = {Fenrir::Physics::ScriptingExtension()};
#endif
    PhysicsDemo::AddDebugCategoryCycling(app);

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
