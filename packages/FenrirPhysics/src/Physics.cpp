#include "FenrirPhysics/Physics.hpp"

#include "Backend/Backend.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScene/Scene.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    struct Physics::State
    {
        State(App& installedOn, const PhysicsConfig& installedWith)
            : app(&installedOn), config(installedWith), layers(installedWith.collisionLayers, *installedOn.Logger()),
              context(*installedOn.Logger())
        {
        }

        App* app;
        PhysicsConfig config;
        CollisionLayers layers;
        Backend::Context context;
    };

    namespace
    {
        // The PostTick system: the active scene's world, made the first time the scene has physics components, steps.
        void StepActiveScene(App& app, const Physics::State& state)
        {
            Scene& scene = app.GetActiveScene();
            PhysicsWorld* world = PhysicsWorld::Of(scene);
            if (world == nullptr)
            {
                EntityList& entities = scene.GetEntityList();
                if (entities.Count(ComponentType::Native<Collider>()) == 0 &&
                    entities.Count(ComponentType::Native<RigidBody>()) == 0)
                {
                    return; // no physics in this scene yet
                }
                world = &scene.AddResource<PhysicsWorld>(std::make_unique<PhysicsWorld::Impl>(
                    entities, Backend::WorldSettings{state.config.gravity}, *app.Logger()));
            }
            world->Implementation().Step(static_cast<float>(app.GetTime().tickRate));
        }

        // The BeforeTicks system: the active scene's bodies are back at their true poses before any tick.
        void RestoreActiveScene(App& app)
        {
            if (PhysicsWorld* world = PhysicsWorld::Of(app.GetActiveScene()))
            {
                world->Implementation().Restore();
            }
        }

        // The AfterTicks system: the active scene's bodies are shown as far between their last two step poses as the
        // frame is through the next tick.
        void BlendActiveScene(App& app, const Physics::State& state)
        {
            PhysicsWorld* world = PhysicsWorld::Of(app.GetActiveScene());
            if (world == nullptr || !state.config.interpolate)
            {
                return;
            }
            const Time& time = app.GetTime();
            world->Implementation().Blend(static_cast<float>(time.accumulator / time.tickRate));
        }
    } // namespace

    Physics::Physics(std::shared_ptr<State> state) : m_state(std::move(state))
    {
    }

    Physics::Physics(Physics&& other) noexcept = default;

    Physics& Physics::operator=(Physics&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    Physics::~Physics()
    {
        Shutdown();
    }

    const CollisionLayers& Physics::Layers() const
    {
        return m_state->layers;
    }

    void Physics::Shutdown() noexcept
    {
        if (!m_state)
        {
            return;
        }
        // Worlds first: the backend's process-wide state must outlive every world.
        m_state->app->ForEachScene([](Scene& scene) { scene.RemoveResource<PhysicsWorld>(); });
        m_state.reset();
    }

    Physics Physics::Install(App& app, PhysicsConfig config)
    {
        auto state = std::make_shared<State>(app, config);
        app.AddSequentialSystems(SchedulePriority::PostTick, {[installed = std::weak_ptr<State>(state)](App& running) {
                                     if (const std::shared_ptr<State> current = installed.lock())
                                     {
                                         StepActiveScene(running, *current);
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::BeforeTicks,
                                 {[installed = std::weak_ptr<State>(state)](App& running) {
                                     if (installed.lock())
                                     {
                                         RestoreActiveScene(running);
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::AfterTicks,
                                 {[installed = std::weak_ptr<State>(state)](App& running) {
                                     if (const std::shared_ptr<State> current = installed.lock())
                                     {
                                         BlendActiveScene(running, *current);
                                     }
                                 }});
        return Physics(std::move(state));
    }
} // namespace Fenrir
