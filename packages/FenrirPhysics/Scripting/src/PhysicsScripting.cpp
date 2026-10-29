#include "FenrirPhysicsScripting/PhysicsScripting.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "Generated/FenrirPhysicsNatives.hpp"
#include "PhysicsScriptingState.hpp"

// Everything C# sees of physics is generated (api/FenrirPhysics.binding.toml): the components', events' and shared
// structs' mappings, checked against the Fenrir.Physics contract whenever a script assembly loads, and from the native
// functions below (described into api/FenrirPhysics.natives.json) their C# declarations, PhysicsNative, and this
// registration of them all.
namespace Fenrir::Generated
{
    void RegisterPhysics(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    namespace
    {
        // A force-like function: queues `apply` on the entity's body in the active scene's world.
        template <void (PhysicsWorld::*Apply)(const Entity&, const Math::Vec3&)>
        bool Push(const CallContext& context, PhysicsScripting& physics, const Entity& entity, const Math::Vec3& amount)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            const std::optional<Entity> body = physics.BodyOf(context, entity);
            if (!body)
            {
                return false;
            }
            (world->*Apply)(*body, amount);
            return true;
        }

        static_assert(sizeof(JointFrames) == 2 * sizeof(JointFrame),
                      "Keep JointFrames as the C# JointFrames lays it out.");
    } // namespace

    // The native functions behind Fenrir.Physics.PhysicsWorld, each named as C# calls it. tools/describe-api reads them
    // from this file, in this order, and Iris.Bindings generates their C# declarations (Fenrir.Physics.PhysicsNative)
    // and their registration from that. A pointer parameter is `in` from C# when const, `out` when its doc marks it an
    // output (`[out]`), and a span when the std::int32_t count follows it. Each trusts the declaration (ADR-0045): an
    // `in` or `out` pointer is never null, and the registration zeroes each single output before the native runs, so
    // one that returns doing nothing (false or zero) with no world or no body writes nothing. Not in an anonymous
    // namespace: the generated natives header declares them.
    namespace PhysicsNatives
    {
        /**
         * @brief Pushes the entity's body with `force` (newtons, world space) for the next step only.
         * @return Whether the force was queued: false with no physics world or no body.
         * @facade PhysicsWorld.AddForce
         */
        bool AddForce(const CallContext& context, PhysicsScripting& physics, const Entity* entity,
                      const Math::Vec3* force)
        {
            return Push<&PhysicsWorld::AddForce>(context, physics, *entity, *force);
        }

        /**
         * @brief Changes the velocity of the entity's body by `impulse` (newton seconds) over its mass, at the next
         * step.
         * @return Whether the impulse was queued: false with no physics world or no body.
         * @facade PhysicsWorld.AddImpulse
         */
        bool AddImpulse(const CallContext& context, PhysicsScripting& physics, const Entity* entity,
                        const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddImpulse>(context, physics, *entity, *impulse);
        }

        /**
         * @brief Twists the entity's body with `torque` (newton metres, world space) for the next step only.
         * @return Whether the torque was queued: false with no physics world or no body.
         * @facade PhysicsWorld.AddTorque
         */
        bool AddTorque(const CallContext& context, PhysicsScripting& physics, const Entity* entity,
                       const Math::Vec3* torque)
        {
            return Push<&PhysicsWorld::AddTorque>(context, physics, *entity, *torque);
        }

        /**
         * @brief Changes the angular velocity of the entity's body by `impulse` over its moment of inertia, at the next
         * step.
         * @return Whether the impulse was queued: false with no physics world or no body.
         * @facade PhysicsWorld.AddAngularImpulse
         */
        bool AddAngularImpulse(const CallContext& context, PhysicsScripting& physics, const Entity* entity,
                               const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddAngularImpulse>(context, physics, *entity, *impulse);
        }

        /**
         * @brief The nearest body a ray hits (PhysicsWorld::Raycast).
         * @param[out] hit Where it hit; the default when it did not.
         * @return Whether it hit a body.
         */
        bool Raycast(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                     const Math::Vec3* direction, float maxDistance, const QueryFilter* filter, RaycastHit* hit)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            const std::optional<RaycastHit> found = world->Raycast(*origin, *direction, maxDistance, *filter);
            if (!found)
            {
                return false;
            }
            *hit = *found;
            return true;
        }

        /**
         * @brief Writes a ray's hits to `hits`, nearest first (PhysicsWorld::RaycastAll).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t RaycastAll(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                                const Math::Vec3* direction, float maxDistance, const QueryFilter* filter,
                                RaycastHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            return static_cast<std::int32_t>(
                world->RaycastAll(*origin, *direction, maxDistance, SpanOf(hits, capacity), *filter));
        }

        /**
         * @brief The first body a swept shape touches (PhysicsWorld::ShapeCast).
         * @param[out] hit Where it touched; the default when it did not.
         * @return Whether it touched a body.
         */
        bool ShapeCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                       const Transform* pose, const Math::Vec3* direction, float maxDistance, const QueryFilter* filter,
                       ShapeCastHit* hit)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            const std::optional<ShapeCastHit> found = world->ShapeCast(*shape, *pose, *direction, maxDistance, *filter);
            if (!found)
            {
                return false;
            }
            *hit = *found;
            return true;
        }

        /**
         * @brief Writes a shape cast's hits to `hits`, nearest first (PhysicsWorld::ShapeCastAll).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t ShapeCastAll(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const Math::Vec3* direction, float maxDistance,
                                  const QueryFilter* filter, ShapeCastHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            return static_cast<std::int32_t>(
                world->ShapeCastAll(*shape, *pose, *direction, maxDistance, SpanOf(hits, capacity), *filter));
        }

        /**
         * @brief How far a shape can move along `motion` and stay clear (PhysicsWorld::MoveCast).
         * @param[out] hit What blocks it; the default when nothing does.
         * @return Whether something blocks the motion.
         */
        bool MoveCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                      const Transform* pose, const Math::Vec3* motion, const QueryFilter* filter, MoveCastHit* hit)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            const std::optional<MoveCastHit> found = world->MoveCast(*shape, *pose, *motion, *filter);
            if (!found)
            {
                return false;
            }
            *hit = *found;
            return true;
        }

        /**
         * @brief Writes the colliders a shape overlaps to `hits` (PhysicsWorld::Overlap).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t Overlap(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                             const Transform* pose, const QueryFilter* filter, OverlapHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            return static_cast<std::int32_t>(world->Overlap(*shape, *pose, SpanOf(hits, capacity), *filter));
        }

        /**
         * @brief Writes the colliders a shape overlaps to `hits`, deepest first, with the way out of each
         * (PhysicsWorld::Penetrations).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t Penetrations(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const QueryFilter* filter, PenetrationHit* hits,
                                  std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            return static_cast<std::int32_t>(world->Penetrations(*shape, *pose, SpanOf(hits, capacity), *filter));
        }

        /**
         * @brief The offset that moves a shape out of everything it overlaps (PhysicsWorld::Depenetrate).
         * @param[out] offset The offset; zero when there is no world.
         * @return Whether there was a world to ask.
         */
        bool Depenetrate(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                         const Transform* pose, const QueryFilter* filter, Math::Vec3* offset)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            *offset = world->Depenetrate(*shape, *pose, *filter);
            return true;
        }

        /**
         * @brief Writes the colliders containing `point` to `hits` (PhysicsWorld::OverlapPoint).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t OverlapPoint(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* point,
                                  const QueryFilter* filter, OverlapHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            return static_cast<std::int32_t>(world->OverlapPoint(*point, SpanOf(hits, capacity), *filter));
        }

        /**
         * @brief Writes the contacts the body entity `body` (a rigid body or a character body) has now, as of the last
         * step, to `contacts` (PhysicsWorld::ForEachContact), stopping when they are full. Overlaps with triggers are
         * not contacts. A character's contacts are those its last move ended touching, so unlike a body's they come
         * from before the step: the walls, ground and ceilings it is against and the bodies it pushed, with their point
         * and normal (a wall jump's).
         * @param contacts Where to write at most `capacity` contacts.
         * @return How many it wrote.
         * @facade PhysicsWorld.ContactsOf
         */
        std::int32_t ContactsOf(const CallContext& context, PhysicsScripting& physics, const Entity* body,
                                Contact* contacts, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            const std::optional<Entity> alive = physics.BodyOf(context, *body);
            if (!alive)
            {
                return 0;
            }
            const std::span<Contact> out = SpanOf(contacts, capacity);
            std::size_t count = 0;
            world->ForEachContact(*alive, [&](const Contact& contact) {
                if (count < out.size())
                {
                    out[count++] = contact;
                }
            });
            return static_cast<std::int32_t>(count);
        }

        /**
         * @brief Whether the body of `body` (a child collider's entity stands for its compound body) is asleep: false
         * with no world or no body.
         * @facade PhysicsWorld.IsSleeping
         */
        bool IsSleeping(const CallContext& context, PhysicsScripting& physics, const Entity* body)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            const std::optional<Entity> alive = physics.BodyOf(context, *body);
            return alive && world->IsSleeping(*alive);
        }

        /**
         * @brief The world-space box around the body of `body` (PhysicsWorld::Bounds).
         * @param[out] bounds The box; the default when there is none.
         * @return Whether the entity has a body; `bounds` is default when it does not.
         * @facade PhysicsWorld.Bounds
         */
        bool Bounds(const CallContext& context, PhysicsScripting& physics, const Entity* body, Math::AABB* bounds)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            const std::optional<Entity> alive = physics.BodyOf(context, *body);
            if (!alive)
            {
                return false;
            }
            const std::optional<Math::AABB> found = world->Bounds(*alive);
            if (!found)
            {
                return false;
            }
            *bounds = *found;
            return true;
        }

        /**
         * @brief The world's gravity.
         * @param[out] gravity The gravity, in metres per second squared; zero with no world.
         * @return Whether there was a world.
         */
        bool Gravity(const CallContext& context, PhysicsScripting& physics, Math::Vec3* gravity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            *gravity = world->Gravity();
            return true;
        }

        /**
         * @brief Sets the world's gravity from the next step on, waking its bodies.
         * @return Whether there was a world.
         */
        bool SetGravity(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* gravity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            world->SetGravity(*gravity);
            return true;
        }

        /**
         * @brief Chooses the categories of debug lines the world draws: `categories` is PhysicsDebugCategories's bits.
         * @return Whether there was a world.
         */
        bool SetDebugDraw(const CallContext& context, PhysicsScripting& physics, std::uint32_t categories)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            world->SetDebugDraw(static_cast<PhysicsDebugCategories>(
                categories & static_cast<std::uint32_t>(PhysicsDebugCategories::All)));
            return true;
        }

        /**
         * @brief Cooks a collision mesh through the active scene's world (PhysicsWorld::CreateCollisionMesh).
         * @param indices May be null when `indexCount` is 0.
         * @return The mesh's id, 0 for none.
         */
        std::uint64_t CreateCollisionMesh(const CallContext& context, PhysicsScripting& physics,
                                          const Math::Vec3* vertices, std::int32_t vertexCount,
                                          const std::uint32_t* indices, std::int32_t indexCount)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return 0;
            }
            // The owner's meshes, which the world reaches, say why they refuse empty or broken data.
            return world->CreateCollisionMesh(SpanOf(vertices, vertexCount), SpanOf(indices, indexCount)).id;
        }

        /**
         * @brief Releases the collision mesh whose id is `mesh` (PhysicsWorld::ReleaseCollisionMesh).
         * @return Whether the mesh was alive.
         */
        bool ReleaseCollisionMesh(const CallContext& context, PhysicsScripting& physics, std::uint64_t mesh)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            // Releasing a mesh that is not alive warns there, every time, as it does in C++.
            return world->ReleaseCollisionMesh(CollisionMesh{mesh});
        }

        /**
         * @brief C++'s FrameFromWorld, once the bodies it needs are alive (C++'s gives identity frames without a word,
         * having no app to warn through); identity frames, warning once, if not. Needs no physics world.
         * @param bodyB No entity (Entity.None in C#) is the world; any other names an entity of the active scene.
         * @param[out] frames The joint's frames; identity frames when a body is not alive.
         */
        void FrameFromWorld(const CallContext& context, PhysicsScripting& physics, const Entity* bodyA,
                            const Entity* bodyB, const JointFrame* anchor, JointFrames* frames)
        {
            EntityList& entities = context.Entities();
            const std::uint32_t a = NativeGuard::ValueOf(*bodyA, entities);
            const std::uint32_t b = NativeGuard::ValueOf(*bodyB, entities);
            const bool world = bodyB->GetId() == Entity::Null;
            const bool aliveA = entities.HasEntity(a);
            if (!aliveA || (!world && !entities.HasEntity(b)))
            {
                const NativeGuard guard(context, physics.warnings);
                guard.Warn(PhysicsScripting::NotAliveFrames(aliveA ? "bodyB" : "bodyA"));
                return;
            }
            // Qualified: this function's own name hides FenrirPhysics's within this namespace.
            *frames = Fenrir::FrameFromWorld(entities.HandleOf(a), world ? Entity() : entities.HandleOf(b), *anchor);
        }

        /**
         * @brief How the joint on `joint` sits and how hard it pulls, as of the last step
         * (PhysicsWorld::GetJointState). Only reads: nothing is written into its Joint. While a joint's bodies sleep,
         * the force and torque keep the values they had when they fell asleep; while it is disabled they are 0. The
         * pose readings are always current. An inactive or unstepped joint reads zeroed from the world itself, which
         * already warns about an inactive one.
         * @param[out] state The reading; the zeroed state (warning once) with no world or no Joint.
         * @facade PhysicsWorld.GetJointState returns state
         */
        void GetJointState(const CallContext& context, PhysicsScripting& physics, const Entity* joint,
                           JointState* state)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return;
            }
            EntityList& entities = context.Entities();
            const std::uint32_t value = NativeGuard::ValueOf(*joint, entities);
            if (!entities.HasEntity(value) || !entities.GetEntity(value).HasComponent<Joint>())
            {
                const NativeGuard guard(context, physics.warnings);
                guard.Warn(std::string(PhysicsScripting::NoJoint));
                return;
            }
            *state = world->GetJointState(entities.HandleOf(value));
        }
    } // namespace PhysicsNatives

    Fenrir::ScriptingExtension Physics::ScriptingExtension()
    {
        return Generated::MakePhysicsExtension(std::make_shared<PhysicsScripting>());
    }
} // namespace Fenrir
