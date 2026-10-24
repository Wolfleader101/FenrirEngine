#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/DebugLines.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Queries.hpp"

namespace Fenrir
{
    class Scene;
    struct PhysicsWorldAccess;
    struct PhysicsProbe;

    /** @brief One contact of a body, as PhysicsWorld::ForEachContact reports it. */
    struct Contact
    {
        /** @brief The body entity (a rigid body or a character body) asked about. */
        Entity body;
        /** @brief The collider entity of `body` touching: `body` itself, or one beneath it in a compound body. */
        Entity collider;
        /** @brief The body entity (a rigid body or a character body) `body` touches. */
        Entity otherBody;
        /** @brief The collider entity of `otherBody` touching. */
        Entity otherCollider;
        /** @brief Where they touch, in world space, as of the last step. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief World space, unit length, pointing from `body` to `otherBody`, as of the last step. */
        Math::Vec3 normal = Math::Vec3(0.0f);
    };

    /**
     * @brief One scene's physics world: the bodies of that scene's colliders, simulated together. A scene resource,
     * made by the systems Physics::Install adds and ended with its scene (or when the Physics owner is destroyed).
     *
     * Queries see the world as the last completed step left it: an ECS change made since (a moved or new collider, a
     * teleport) is seen from the next tick on. Queries only read, so systems running in parallel may query one world
     * at once; physics's own systems, which sync and step the world, never run beside them. A body whose entity was
     * destroyed since the last step is not found.
     *
     * A call that takes an entity ignores a handle to one no longer alive (destroyed, its id perhaps reused by a newer
     * entity): it finds no body, pushes nothing and ignores nothing.
     */
    class PhysicsWorld
    {
      public:
        /** @brief The world's state, defined in FenrirPhysics's private headers: only FenrirPhysics can make one. */
        struct Impl;

        /**
         * @brief Proof that FenrirPhysics is making the world: only PhysicsWorldAccess, in FenrirPhysics's private
         * headers, can make a Key. Public only because Scene::AddResource constructs the world.
         */
        class Key
        {
            Key() = default;
            friend struct PhysicsWorldAccess;
        };

        /** @brief Made by FenrirPhysics (it alone holds a Key); game code reaches a world through Of. */
        PhysicsWorld(Key key, std::unique_ptr<Impl> impl);
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        /** @brief The scene's physics world, or null if the scene has not yet run as the active scene with physics. */
        [[nodiscard]] static PhysicsWorld* Of(Scene& scene);

        /**
         * @brief Calls `func` once for each contact `entity`'s body (for a child collider's entity, the compound body
         * it belongs to) has now (as of the last step), whether or not contacts are reported as events. Contacts are as
         * ContactBegin and ContactEnd tell them: one resting on another that has fallen asleep is still touching.
         * Overlaps with triggers are not contacts. None for an entity no longer alive.
         *
         * A character's contacts are those its last move ended touching, so unlike a body's they come from before the
         * step: the walls, ground and ceilings it is against, and the bodies it pushed, with their point and normal (a
         * wall jump's). A dynamic body that met only the character's body in the step lists it, but is not listed for
         * the character.
         */
        void ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const;

        /**
         * @brief The nearest body a ray from `origin` along `direction` (any length) hits within `maxDistance`. A ray
         * that starts inside a box, sphere, capsule or convex hull hits it at distance 0. A triangle mesh is hit only
         * on its triangles' front faces (counter-clockwise seen from the ray), so a ray from inside a closed mesh
         * passes out through it.
         */
        [[nodiscard]] std::optional<RaycastHit> Raycast(const Math::Vec3& origin, const Math::Vec3& direction,
                                                        float maxDistance, const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the hits of a ray, as for Raycast, to `hits`, nearest first, and returns how many it wrote.
         * When there are more hits than `hits` holds, the nearest ones are written.
         */
        std::size_t RaycastAll(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                               std::span<RaycastHit> hits, const QueryFilter& filter = {}) const;

        /**
         * @brief The first body `shape`, placed at `pose` (scaled as a collider is), touches when swept along
         * `direction` (any length) for up to `maxDistance`. A shape that starts touching a body hits it at distance 0.
         */
        [[nodiscard]] std::optional<ShapeCastHit> ShapeCast(const CollisionShape& shape, const Transform& pose,
                                                            const Math::Vec3& direction, float maxDistance,
                                                            const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the hits of a shape cast, as for ShapeCast, to `hits`, nearest first, and returns how many it
         * wrote. When there are more hits than `hits` holds, the nearest ones are written.
         */
        std::size_t ShapeCastAll(const CollisionShape& shape, const Transform& pose, const Math::Vec3& direction,
                                 float maxDistance, std::span<ShapeCastHit> hits, const QueryFilter& filter = {}) const;

        /**
         * @brief How far `shape`, placed at `pose` (scaled as a collider is), can move along `motion` (a displacement:
         * its length is how far) and stay clear, or none if the whole motion is clear. Moved `distance` along
         * `motion`, the shape keeps a small gap (the skin, a fixed 0.02 m) from what blocked it, measured along that
         * body's normal: a glancing hit stops it further back than a head-on one.
         *
         * Bodies the shape starts inside or touching, and bodies it moves away from, do not block it, so a shape
         * resting on the ground can slide along it. A shape a MoveCast placed keeps the skin's gap, so moving it on
         * into what blocked it is blocked at once (distance 0). Free a shape that starts inside a body with
         * Depenetrate.
         *
         * To move a shape as a body would, filter as it does: `{ .mask = cf.mask, .layers = cf.layers,
         * .ignoreBody = me }`, with `cf` its CollisionFilter (or a default one). A triangle-mesh shape (MeshShape)
         * finds nothing.
         */
        [[nodiscard]] std::optional<MoveCastHit> MoveCast(const CollisionShape& shape, const Transform& pose,
                                                          const Math::Vec3& motion,
                                                          const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the colliders `shape`, placed at `pose` (scaled as a collider is), overlaps to `hits`, each
         * with its body, and returns how many it wrote, stopping when `hits` is full.
         */
        std::size_t Overlap(const CollisionShape& shape, const Transform& pose, std::span<OverlapHit> hits,
                            const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the colliders `shape`, placed at `pose` (scaled as a collider is), overlaps to `hits`, each
         * with its body, which way out of it and how deep, and returns how many it wrote. Deepest first, a collider
         * once at its deepest; when there are more than `hits` holds, the deepest are written. A shape only touching
         * a collider does not overlap it. A triangle-mesh shape (MeshShape) finds nothing.
         */
        std::size_t Penetrations(const CollisionShape& shape, const Transform& pose, std::span<PenetrationHit> hits,
                                 const QueryFilter& filter = {}) const;

        /**
         * @brief The offset that moves `shape`, placed at `pose` (scaled as a collider is), out of everything it
         * overlaps, leaving the skin's gap MoveCast leaves; zero when it overlaps nothing. Several overlaps are
         * resolved together, over a few tries. Wedged in a crevice the shape may still overlap something once
         * moved: check with Penetrations when that matters. A triangle-mesh shape (MeshShape) finds nothing.
         */
        [[nodiscard]] Math::Vec3 Depenetrate(const CollisionShape& shape, const Transform& pose,
                                             const QueryFilter& filter = {}) const;

        /** @brief Writes the colliders containing `point` to `hits`, each with its body, and returns how many it wrote,
         * stopping when `hits` is full. */
        std::size_t OverlapPoint(const Math::Vec3& point, std::span<OverlapHit> hits,
                                 const QueryFilter& filter = {}) const;

        /** @brief Whether `entity`'s body (the entity with its RigidBody or Collider; a child collider's entity stands
         * for the compound body it belongs to) is asleep: resting, and not simulated until something wakes it. False
         * for an entity with no body or no longer alive. */
        [[nodiscard]] bool IsSleeping(const Entity& entity) const;

        /** @brief The world-space box around `entity`'s body, or none for an entity with no body or no longer alive.
         */
        [[nodiscard]] std::optional<Math::AABB> Bounds(const Entity& entity) const;

        /**
         * @brief How the joint on `joint` sits and how hard it pulls, as of the last step (see JointState). Never
         * written into its Joint; only reads, like the other queries.
         *
         * Zeroed for an entity with no Joint or no longer alive, an inactive joint, and a joint made
         * since the last step. While a joint's bodies sleep, `force` and `torque` keep the values they had when they
         * fell asleep; while it is disabled they are 0. The pose readings are always current.
         */
        [[nodiscard]] JointState GetJointState(const Entity& joint) const;

        /**
         * @brief Changes the velocity of `entity`'s body (for a child collider's entity, the compound body it belongs
         * to) by `impulse` (newton seconds, world space) over its mass, at the next step. Does nothing if the entity
         * is no longer alive, or has no dynamic body then.
         */
        void AddImpulse(const Entity& entity, const Math::Vec3& impulse);

        /**
         * @brief Pushes `entity`'s body with `force` (newtons, world space, through its centre of mass) for the next
         * step only: add it every tick to keep pushing. Does nothing if the entity is no longer alive, or has no
         * dynamic body then.
         */
        void AddForce(const Entity& entity, const Math::Vec3& force);

        /**
         * @brief Twists `entity`'s body with `torque` (newton metres, world space) for the next step only: add it every
         * tick to keep twisting. Does nothing if the entity is no longer alive, or has no dynamic body then.
         */
        void AddTorque(const Entity& entity, const Math::Vec3& torque);

        /**
         * @brief Changes the angular velocity of `entity`'s body by `impulse` (newton metre seconds, world space) over
         * its moment of inertia, at the next step. Does nothing if the entity is no longer alive, or has no dynamic
         * body then.
         */
        void AddAngularImpulse(const Entity& entity, const Math::Vec3& impulse);

        /**
         * @brief Cooks a collision mesh, as Physics::CreateCollisionMesh does: the meshes are the Physics owner's and
         * are shared by every world, so a mesh made here is usable by a collider in any scene. A world only reaches
         * them, for a C++ system or a script that holds a world and not the owner.
         */
        [[nodiscard]] CollisionMesh CreateCollisionMesh(std::span<const Math::Vec3> vertices,
                                                        std::span<const std::uint32_t> indices);

        /** @brief Releases a collision mesh, as Physics::ReleaseCollisionMesh does, whichever world or the owner made
         * it. */
        bool ReleaseCollisionMesh(CollisionMesh mesh);

        /** @brief The world's gravity, in metres per second squared. A new world starts with PhysicsConfig::gravity. */
        [[nodiscard]] Math::Vec3 Gravity() const;

        /** @brief Changes the world's gravity from the next step on, waking its bodies. Other worlds keep theirs. */
        void SetGravity(const Math::Vec3& gravity);

        /**
         * @brief Chooses which categories of debug lines the world draws, from the next step on. None (the default)
         * draws nothing and costs one check per step.
         *
         * After each step the world draws the lines of these categories, made from the world as the step left it, into
         * its scene's DebugDraw (FenrirDebugDraw, ADR-0025), in the PhysicsDebugGroup and with duration 0, so they
         * last until the next tick. Hide that group to hide them. In an app without DebugDrawing installed the scene
         * has no DebugDraw, and nothing is drawn.
         *
         * They show each body at its true step pose. Between ticks an interpolated body's Transform is blended between
         * its last two step poses, so its rendered model may lag its lines by up to one step.
         */
        void SetDebugDraw(PhysicsDebugCategories categories);

        /** @brief The categories of debug lines the world draws. */
        [[nodiscard]] PhysicsDebugCategories DebugDrawCategories() const;

      private:
        // FenrirPhysics's own systems step the world through its state; the tests read a diagnostic.
        friend struct PhysicsWorldAccess;
        friend struct PhysicsProbe;

        /** @brief Diagnostic for FenrirPhysics's tests, read through PhysicsProbe: how many distinct backend shapes the
         * world's bodies use. Colliders with identical primitive shapes (after scale) share one. */
        [[nodiscard]] std::size_t DistinctShapeCount() const;

        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir
