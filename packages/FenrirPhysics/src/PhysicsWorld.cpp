#include "FenrirPhysics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirScene/Scene.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Enough collision steps that none is longer than a 60 Hz step.
        int CollisionSteps(float dt)
        {
            return std::max(1, static_cast<int>(std::ceil(dt * 60.0f - 1e-4f)));
        }
    } // namespace

    PhysicsWorld::PhysicsWorld(Key /*key*/, std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
    {
    }

    std::size_t PhysicsWorld::DistinctShapeCount() const
    {
        return m_impl->m_backend.DistinctShapeCount();
    }

    PhysicsWorld::~PhysicsWorld() = default;

    PhysicsWorld* PhysicsWorld::Of(Scene& scene)
    {
        return scene.GetResource<PhysicsWorld>();
    }

    void PhysicsWorld::AddImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity, &Backend::World::AddImpulse, impulse);
    }

    void PhysicsWorld::AddForce(const Entity& entity, const Math::Vec3& force)
    {
        m_impl->Queue(entity, &Backend::World::AddForce, force);
    }

    void PhysicsWorld::AddTorque(const Entity& entity, const Math::Vec3& torque)
    {
        m_impl->Queue(entity, &Backend::World::AddTorque, torque);
    }

    void PhysicsWorld::AddAngularImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity, &Backend::World::AddAngularImpulse, impulse);
    }

    Math::Vec3 PhysicsWorld::Gravity() const
    {
        return m_impl->Gravity();
    }

    void PhysicsWorld::SetGravity(const Math::Vec3& gravity)
    {
        m_impl->SetGravity(gravity);
    }

    PhysicsWorld::Impl::Impl(EntityList& entities, const Backend::WorldSettings& settings,
                             Backend::CollisionMeshes& meshes, ILogger& logger)
        : m_entities(&entities), m_backend(settings, meshes, logger), m_poses(entities),
          m_bodies(entities, logger, m_backend, m_poses), m_contacts(entities, m_backend, m_bodies),
          m_joints(entities, logger, m_backend, m_bodies, m_poses), m_characters(entities, logger, m_backend, m_poses),
          m_vehicles(entities, logger, m_backend, m_bodies, m_poses), m_canvas(m_backend)
    {
    }

    void PhysicsWorld::ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const
    {
        m_impl->ForEachContact(entity, func);
    }

    void PhysicsWorld::Impl::Step(App& app, float dt)
    {
        // Drain and rebuild. A joint's constraint and a vehicle go before their bodies, and characters go before the
        // bodies' batch and are made after it, so the bodies come in a reproducible order.
        const Detail::Bodies::Rebuild rebuild = m_bodies.Plan();
        m_joints.Drain();
        m_vehicles.Drain();
        m_joints.DropOn(rebuild.destroyed);
        m_vehicles.DropOn(rebuild.destroyed);
        m_characters.Drop(rebuild.changed);
        m_bodies.Apply(rebuild);
        m_characters.Make();

        // Edits. After the bodies': a body's scale decides whether its joints are recreated, and a vehicle is made
        // after its body and its body's joints.
        m_bodies.PushEdits(dt);
        m_characters.PushEdits();
        m_joints.Sync(dt);
        m_vehicles.Sync();
        m_bodies.ApplyPushes();

        // Moves: before the step, so a character's pushes act in it and it moves against the bodies the step will
        // move.
        m_characters.Move(dt);

        m_backend.Step(dt, CollisionSteps(dt));

        // Write-back: bodies, characters and wheels through Poses, parents first, each wheel after its chassis and
        // against its new pose.
        m_bodies.HandPoses();
        m_characters.HandPoses();
        m_vehicles.WriteBack(dt);
        m_poses.WriteBack();
        m_characters.ResetIdle();

        // Joints after the step.
        m_joints.Measure();
        m_joints.Break();

        // Events, then the shown poses and the debug lines.
        m_contacts.Send(app, m_characters.MoveContacts());
        m_joints.SendBroken(app);
        m_poses.Advance();
        DrawDebugLines();
    }

    void PhysicsWorld::Impl::Restore()
    {
        m_poses.Restore();
    }

    void PhysicsWorld::Impl::Blend(float fraction)
    {
        // Wheels are handed first, on their chassis's blended pose, for Poses to write after the bodies.
        m_vehicles.BlendWheels(fraction);
        m_poses.Blend(fraction);
    }

    void PhysicsWorld::SetDebugDraw(DebugDraw categories)
    {
        m_impl->m_debugDraw = categories;
    }

    DebugDraw PhysicsWorld::DebugDrawCategories() const
    {
        return m_impl->m_debugDraw;
    }

    std::span<const DebugLine> PhysicsWorld::DebugLines() const
    {
        return m_impl->m_canvas.Lines();
    }

    void PhysicsWorld::Impl::DrawDebugLines()
    {
        if (m_debugDraw == DebugDraw::None)
        {
            m_canvas.Clear();
            return;
        }
        m_canvas.Begin();
        if (Includes(m_debugDraw, DebugDraw::Colliders))
        {
            m_bodies.DrawColliders(m_canvas);
        }
        if (Includes(m_debugDraw, DebugDraw::Joints))
        {
            m_joints.Draw(m_canvas);
        }
        if (Includes(m_debugDraw, DebugDraw::Characters))
        {
            m_characters.Draw(m_canvas);
        }
        if (Includes(m_debugDraw, DebugDraw::Vehicles))
        {
            m_vehicles.Draw(m_canvas);
        }
        if (Includes(m_debugDraw, DebugDraw::Contacts))
        {
            m_contacts.Draw(m_canvas);
        }
        m_bodies.DrawMotion(m_canvas, Includes(m_debugDraw, DebugDraw::Velocities),
                            Includes(m_debugDraw, DebugDraw::Bounds));
        m_canvas.End();
    }

    void PhysicsWorld::Impl::ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const
    {
        m_contacts.ForEachContact(entity, m_characters.MoveContactsOf(entity), func);
    }
} // namespace Fenrir
