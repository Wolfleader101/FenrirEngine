// PROTOTYPE (physics spike): Physics::Install and the owner of what it set up.

#include "FenrirPhysics/Physics.hpp"

#include "Backend/Backend.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    struct Physics::State
    {
        App* app;
        PhysicsConfig config;
    };

    Physics::Physics(std::unique_ptr<State> state) : m_state(std::move(state))
    {
    }

    Physics::Physics(Physics&& other) noexcept = default;
    Physics& Physics::operator=(Physics&& other) noexcept = default;

    Physics::~Physics()
    {
        if (!m_state)
        {
            return;
        }
        // Worlds first: the backend's process-wide state must outlive every world.
        m_state->app->ForEachScene([](Scene& scene) { scene.RemoveResource<PhysicsWorld>(); });
        Backend::Release();
    }

    Physics Physics::Install(App& app, PhysicsConfig config)
    {
        Backend::Acquire();
        auto state = std::make_unique<State>(State{&app, config});
        State* installed = state.get();

        // Spike: the system keeps a pointer to the state, so the owner must outlive App::Run.
        app.AddSequentialSystems(SchedulePriority::PostTick, {[installed](App& running) {
                                     Scene& scene = running.GetActiveScene();
                                     PhysicsWorld* world = PhysicsWorld::Of(scene);
                                     if (!world)
                                     {
                                         if (scene.GetEntityList().Count(ComponentType::Native<Collider>()) == 0)
                                         {
                                             return; // no physics in this scene yet
                                         }
                                         world = &scene.AddResource<PhysicsWorld>(installed->config);
                                     }
                                     world->Step(scene, static_cast<float>(running.GetTime().tickRate));
                                 }});
        return Physics(std::move(state));
    }
} // namespace Fenrir
