#include "FenrirPhysics/Physics.hpp"

#include <cassert>

#include "Backend/Backend.hpp"
#include "BodyComponents.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScene/Scene.hpp"
#include "PhysicsState.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // The PostTick system: the active scene's world, made the first time the scene has physics components, steps.
        void StepActiveScene(App& app, Detail::PhysicsState& state)
        {
            Scene& scene = app.GetActiveScene();
            PhysicsWorld* world = PhysicsWorld::Of(scene);
            if (world == nullptr)
            {
                EntityList& entities = scene.GetEntityList();
                if (!HasBodyComponents(entities))
                {
                    return; // no physics in this scene yet
                }
                const PhysicsConfig& config = state.config;
                const Backend::WorldSettings settings{.gravity = config.gravity,
                                                      .maxBodies = config.maxBodies,
                                                      .maxBodyPairs = config.maxBodyPairs,
                                                      .maxContacts = config.maxContacts,
                                                      .workers =
                                                          config.useWorkerThreads ? &app.GetThreadPool() : nullptr};
                world = &PhysicsWorldAccess::Make(
                    scene, std::make_unique<PhysicsWorld::Impl>(entities, settings, state.meshes, *app.Logger()));
            }
            PhysicsWorldAccess::Of(*world).Step(app, static_cast<float>(app.GetTime().tickRate));
        }

        // The BeforeTicks system: the active scene's bodies are back at their true poses before any tick.
        void RestoreActiveScene(App& app)
        {
            if (PhysicsWorld* world = PhysicsWorld::Of(app.GetActiveScene()))
            {
                PhysicsWorldAccess::Of(*world).Restore();
            }
        }

        // The AfterTicks system: the active scene's bodies are shown as far between their last two step poses as the
        // frame is through the next tick.
        void BlendActiveScene(App& app, const Detail::PhysicsState& state)
        {
            PhysicsWorld* world = PhysicsWorld::Of(app.GetActiveScene());
            if (world == nullptr || !state.config.interpolate)
            {
                return;
            }
            const Time& time = app.GetTime();
            PhysicsWorldAccess::Of(*world).Blend(static_cast<float>(time.accumulator / time.tickRate));
        }
    } // namespace

    Physics::Physics(std::shared_ptr<Detail::PhysicsState> state) : m_state(std::move(state))
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
        auto state = std::make_shared<Detail::PhysicsState>(app, config);
        app.AddSequentialSystems(SchedulePriority::PostTick,
                                 {[installed = std::weak_ptr<Detail::PhysicsState>(state)](App& running) {
                                     if (const std::shared_ptr<Detail::PhysicsState> current = installed.lock())
                                     {
                                         StepActiveScene(running, *current);
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::BeforeTicks,
                                 {[installed = std::weak_ptr<Detail::PhysicsState>(state)](App& running) {
                                     if (installed.lock())
                                     {
                                         RestoreActiveScene(running);
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::AfterTicks,
                                 {[installed = std::weak_ptr<Detail::PhysicsState>(state)](App& running) {
                                     if (const std::shared_ptr<Detail::PhysicsState> current = installed.lock())
                                     {
                                         BlendActiveScene(running, *current);
                                     }
                                 }});
        return Physics(std::move(state));
    }
} // namespace Fenrir
