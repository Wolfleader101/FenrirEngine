#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "PhysicsScenes.hpp"
#include "SceneLooks.hpp"

#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#include "FenrirRagdollScripting/RagdollScripting.hpp"
#include "FenrirSkeletonScripting/SkeletonScripting.hpp"
#endif

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

    // Before the scaffold, so it ends after it: the scaffold destroys the active scene, its physics world with it, and
    // ends scripting, which is given physics's extension, while physics is still there.
    Fenrir::Physics physics = Fenrir::Physics::Install(app);
    // The ragdoll glue, wherever physics is (ADR-0053). It does nothing until a scene has a Ragdoll.
    Fenrir::Ragdolls ragdolls = Fenrir::Ragdolls::Install(app);

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
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    config.scenes.push_back(PhysicsDemo::VehiclesCSharp());
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
    // Physics from C#: the physics components, PhysicsWorld's functions and the Fenrir.Physics contract; ragdolls beside
    // it, after the skeleton extension whose bone names the Fenrir.Ragdoll contract uses.
    config.scriptingExtensions = {Fenrir::Physics::ScriptingExtension(), Fenrir::Skeletons::ScriptingExtension(),
                                  Fenrir::Ragdolls::ScriptingExtension()};
#endif
    PhysicsDemo::AddDebugCategoryCycling(app);

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
