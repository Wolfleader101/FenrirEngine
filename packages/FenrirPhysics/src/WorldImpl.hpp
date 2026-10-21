#pragma once

// A physics world: its backend, its feature modules and the PostTick pipeline that calls them by name. Knows the ECS,
// not Jolt: it talks to Backend::World.

#include <functional>
#include <memory>
#include <utility>

#include "Backend/Backend.hpp"
#include "Bodies.hpp"
#include "Characters.hpp"
#include "Contacts.hpp"
#include "DebugCanvas.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirScene/Scene.hpp"
#include "Joints.hpp"
#include "Poses.hpp"
#include "Vehicles.hpp"

namespace Fenrir
{
    class App;
    class EntityList;
    class ILogger;

    struct PhysicsWorld::Impl
    {
        /** @brief A world for the bodies in `entities`, including the colliders already there. */
        Impl(EntityList& entities, const Backend::WorldSettings& settings, Backend::CollisionMeshes& meshes,
             ILogger& logger);

        /**
         * @brief One tick: make and remove bodies and joints for the components added and removed since the last tick
         * (a joint's constraint and a vehicle before their bodies go, and after they come), push the edits game code
         * made to components and world transforms, bodies before joints and joints before vehicles, step by `dt`
         * seconds, write dynamic bodies' poses and velocities back and then vehicles' states and wheel poses, break
         * overloaded joints, and send the step's contact, trigger and JointBroken events to `app`.
         */
        void Step(App& app, float dt);

        /** @brief Calls `func` for each contact of `entity`'s body touching now. */
        void ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const;

        /**
         * @brief `BeforeTicks`: put back the true pose of each body and character Blend wrote, so `Tick` systems and
         * the diff see real simulation state. The parts of a `Transform` game code changed since keep the game's
         * values, and the rest go back to the true pose. The next step takes a changed body as a teleport, and a
         * character only if its position changed. Wheels go back to their true poses whatever game code wrote.
         */
        void Restore();

        /**
         * @brief `AfterTicks`: write each interpolated dynamic or kinematic body's and character's pose `fraction` of
         * the way from its previous step pose to its current one, parents before children. One that is not
         * interpolated, beneath one that is, is written at its step pose, so its parent's blend does not carry it.
         * Then each interpolated chassis's wheels, blended with it (BlendWheels).
         */
        void Blend(float fraction);

        using Pusher = Detail::Bodies::Pusher;

        [[nodiscard]] Math::Vec3 Gravity() const
        {
            return m_backend.Gravity();
        }

        void SetGravity(const Math::Vec3& gravity)
        {
            m_backend.SetGravity(gravity);
        }

        /** @brief Queues a push of `entity`'s body for the next step; nothing for a handle to an entity no longer
         * alive (destroyed, its id perhaps reused by a newer entity). */
        void Queue(const Entity& entity, Pusher pusher, const Math::Vec3& amount)
        {
            m_bodies.Queue(entity, pusher, amount);
        }

      private:
        // The queries, in Queries.cpp, read the bodies and the backend.
        friend class PhysicsWorld;

        using Body = Detail::Bodies::Body;

        /** @brief What the queries read of the bodies. */
        [[nodiscard]] const Detail::Bodies& BodyView() const
        {
            return m_bodies;
        }

        /** @brief Debug lines, remade after each step from the categories switched on, if any: each module draws its
         * own, in a fixed order. */
        void DrawDebugLines();

        /** @brief The backend's form of a query's filter: its ignored body resolved as ResolveBody does, and a handle
         * to an entity no longer alive ignoring nothing. */
        [[nodiscard]] Backend::QueryFilter ToBackend(const QueryFilter& filter) const;

        EntityList* m_entities;
        Backend::World m_backend;
        Detail::Poses m_poses;
        Detail::Bodies m_bodies;
        Detail::Contacts m_contacts;
        Detail::Joints m_joints;
        Detail::Characters m_characters;
        Detail::Vehicles m_vehicles;

        // The debug lines switched on, and those the last step made.
        DebugDraw m_debugDraw = DebugDraw::None;
        Detail::DebugCanvas m_canvas;
    };

    /**
     * @brief FenrirPhysics's own way into a PhysicsWorld, for its systems: the only maker of PhysicsWorld::Key, and
     * the only reader of a world's state outside PhysicsWorld's members.
     */
    struct PhysicsWorldAccess
    {
        /** @brief Makes `scene`'s physics world, holding `impl`, as a scene resource. */
        static PhysicsWorld& Make(Scene& scene, std::unique_ptr<PhysicsWorld::Impl> impl)
        {
            return scene.AddResource<PhysicsWorld>(PhysicsWorld::Key{}, std::move(impl));
        }

        /** @brief The state of `world`. */
        [[nodiscard]] static PhysicsWorld::Impl& Of(PhysicsWorld& world)
        {
            return *world.m_impl;
        }
    };
} // namespace Fenrir
