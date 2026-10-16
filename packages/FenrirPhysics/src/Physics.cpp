#include "FenrirPhysics/Physics.hpp"

#include <cassert>

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
              context(*installedOn.Logger()), meshes(*installedOn.Logger())
        {
        }

        App* app;
        PhysicsConfig config;
        CollisionLayers layers;
        Backend::Context context;
        // After the context, so it ends first: its shapes need the backend's allocator.
        Backend::CollisionMeshes meshes;
    };

    namespace
    {
        // The PostTick system: the active scene's world, made the first time the scene has physics components, steps.
        void StepActiveScene(App& app, Physics::State& state)
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
                    entities,
                    Backend::WorldSettings{.gravity = state.config.gravity,
                                           .workers = state.config.useWorkerThreads ? &app.GetThreadPool() : nullptr},
                    state.meshes, *app.Logger()));
            }
            world->Implementation().Step(app, static_cast<float>(app.GetTime().tickRate));
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

    CollisionMesh Physics::CreateCollisionMesh(std::span<const Math::Vec3> vertices,
                                               std::span<const std::uint32_t> indices)
    {
        assert(m_state && "Physics::CreateCollisionMesh: this Physics was moved from");
        return m_state->meshes.Create(vertices, indices);
    }

    bool Physics::ReleaseCollisionMesh(CollisionMesh mesh)
    {
        assert(m_state && "Physics::ReleaseCollisionMesh: this Physics was moved from");
        return m_state->meshes.Release(mesh);
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
        return Physics(std::move(state));
    }
} // namespace Fenrir
