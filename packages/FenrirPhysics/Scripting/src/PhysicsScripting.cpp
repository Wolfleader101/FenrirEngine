#include "FenrirPhysicsScripting/PhysicsScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <vector>

#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "PhysicsScriptingState.hpp"
#include "ScriptQueries.hpp"

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
        // The query types as C# holds them, with each entity as its 32-bit value (ScriptQueries.hpp), which the natives
        // convert to and from FenrirPhysics's own.
        namespace Script = ScriptQueries;

        // A force-like function: queues `apply` on the entity's body in the active scene's world.
        template <void (PhysicsWorld::*Apply)(const Entity&, const Math::Vec3&)>
        bool Push(const CallContext& context, PhysicsScripting& physics, Script::EntityId entity,
                  const Math::Vec3* amount, std::source_location where = std::source_location::current())
        {
            PhysicsWorld* world = physics.WorldOf(context, where);
            if (world == nullptr || !physics.HasBody(context, entity, where) || amount == nullptr)
            {
                return false;
            }
            (world->*Apply)(context.Entities().HandleOf(entity), *amount);
            return true;
        }

        QueryFilter ToFilter(EntityList& entities, const Script::QueryFilter* filter)
        {
            // C#'s default filter, every byte zero, is the new filter (ScriptQueries.hpp).
            if (filter == nullptr ||
                (filter->mask == 0 && filter->layers == 0 && filter->ignoreBody.value == 0 && !filter->hitTriggers))
            {
                return {};
            }
            return QueryFilter{.mask = filter->mask,
                               .layers = filter->layers,
                               .ignoreBody = filter->ignoreBody.value == Entity::Null
                                                 ? Entity()
                                                 : entities.HandleOf(filter->ignoreBody.value),
                               .hitTriggers = filter->hitTriggers};
        }

        // A RaycastHit or ShapeCastHit as C# holds it.
        template <typename Out, typename Hit>
        Out ToScript(const Hit& hit)
        {
            return Out{.body = Script::EntityValue(hit.body.GetId()),
                       .collider = Script::EntityValue(hit.collider.GetId()),
                       .point = hit.point,
                       .normal = hit.normal,
                       .distance = hit.distance};
        }

        // Where an all-hit query writes in C++ before its hits are copied to C#'s span: `capacity` hits, none if
        // negative.
        template <typename Hit>
        std::vector<Hit> Buffer(std::int32_t capacity)
        {
            return std::vector<Hit>(static_cast<std::size_t>(std::max(capacity, 0)));
        }

        Script::OverlapHit ToScriptOverlap(const OverlapHit& hit)
        {
            return Script::OverlapHit{.body = Script::EntityValue(hit.body.GetId()),
                                      .collider = Script::EntityValue(hit.collider.GetId())};
        }

        static_assert(sizeof(JointFrames) == 2 * sizeof(JointFrame),
                      "Keep JointFrames as the C# JointFrames lays it out.");
    } // namespace

    // The native functions behind Fenrir.Physics.PhysicsWorld, each named as C# calls it. tools/describe-api reads them
    // from this file, in this order, and Iris.Bindings generates their C# declarations (Fenrir.Physics.PhysicsNative)
    // and their registration from that. A pointer parameter is `in` from C# when const, `out` when its doc marks it an
    // output (`[out]`), and a span when the std::int32_t count follows it. Each returns doing nothing (false or zero)
    // with no world, no body, or a null input, having written its output's default first. Not in an anonymous
    // namespace: the generated registration names them from another file.
    namespace PhysicsNatives
    {
        /** @brief Pushes the entity's body with `force` (newtons, world space) for the next step only. */
        bool AddForce(const CallContext& context, PhysicsScripting& physics, Script::EntityId entity,
                      const Math::Vec3* force)
        {
            return Push<&PhysicsWorld::AddForce>(context, physics, entity, force);
        }

        /** @brief Changes the velocity of the entity's body by `impulse` (newton seconds) over its mass, at the next
         * step. */
        bool AddImpulse(const CallContext& context, PhysicsScripting& physics, Script::EntityId entity,
                        const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddImpulse>(context, physics, entity, impulse);
        }

        /** @brief Twists the entity's body with `torque` (newton metres, world space) for the next step only. */
        bool AddTorque(const CallContext& context, PhysicsScripting& physics, Script::EntityId entity,
                       const Math::Vec3* torque)
        {
            return Push<&PhysicsWorld::AddTorque>(context, physics, entity, torque);
        }

        /** @brief Changes the angular velocity of the entity's body by `impulse` over its moment of inertia. */
        bool AddAngularImpulse(const CallContext& context, PhysicsScripting& physics, Script::EntityId entity,
                               const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddAngularImpulse>(context, physics, entity, impulse);
        }

        /**
         * @brief The nearest body a ray hits (PhysicsWorld::Raycast).
         * @param[out] hit Where it hit; the default when it did not.
         * @return Whether it hit a body.
         */
        bool Raycast(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                     const Math::Vec3* direction, float maxDistance, const Script::QueryFilter* filter,
                     Script::RaycastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Script::RaycastHit{};
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || origin == nullptr || direction == nullptr)
            {
                return false;
            }
            const std::optional<RaycastHit> found =
                world->Raycast(*origin, *direction, maxDistance, ToFilter(context.Entities(), filter));
            if (!found)
            {
                return false;
            }
            *hit = ToScript<Script::RaycastHit>(*found);
            return true;
        }

        /**
         * @brief Writes a ray's hits to `hits`, nearest first (PhysicsWorld::RaycastAll).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t RaycastAll(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                                const Math::Vec3* direction, float maxDistance, const Script::QueryFilter* filter,
                                Script::RaycastHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || origin == nullptr || direction == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<RaycastHit> found = Buffer<RaycastHit>(capacity);
            const std::size_t count =
                world->RaycastAll(*origin, *direction, maxDistance, found, ToFilter(context.Entities(), filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           ToScript<Script::RaycastHit, RaycastHit>);
            return static_cast<std::int32_t>(count);
        }

        /**
         * @brief The first body a swept shape touches (PhysicsWorld::ShapeCast).
         * @param[out] hit Where it touched; the default when it did not.
         * @return Whether it touched a body.
         */
        bool ShapeCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                       const Transform* pose, const Math::Vec3* direction, float maxDistance,
                       const Script::QueryFilter* filter, Script::ShapeCastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Script::ShapeCastHit{};
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || shape == nullptr || pose == nullptr || direction == nullptr)
            {
                return false;
            }
            const std::optional<ShapeCastHit> found =
                world->ShapeCast(*shape, *pose, *direction, maxDistance, ToFilter(context.Entities(), filter));
            if (!found)
            {
                return false;
            }
            *hit = ToScript<Script::ShapeCastHit>(*found);
            return true;
        }

        /**
         * @brief Writes a shape cast's hits to `hits`, nearest first (PhysicsWorld::ShapeCastAll).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t ShapeCastAll(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const Math::Vec3* direction, float maxDistance,
                                  const Script::QueryFilter* filter, Script::ShapeCastHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || shape == nullptr || pose == nullptr || direction == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<ShapeCastHit> found = Buffer<ShapeCastHit>(capacity);
            const std::size_t count = world->ShapeCastAll(*shape, *pose, *direction, maxDistance, found,
                                                          ToFilter(context.Entities(), filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           ToScript<Script::ShapeCastHit, ShapeCastHit>);
            return static_cast<std::int32_t>(count);
        }

        /**
         * @brief How far a shape can move along `motion` and stay clear (PhysicsWorld::MoveCast).
         * @param[out] hit What blocks it; the default when nothing does.
         * @return Whether something blocks the motion.
         */
        bool MoveCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                      const Transform* pose, const Math::Vec3* motion, const Script::QueryFilter* filter,
                      Script::MoveCastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Script::MoveCastHit{};
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || shape == nullptr || pose == nullptr || motion == nullptr)
            {
                return false;
            }
            const std::optional<MoveCastHit> found =
                world->MoveCast(*shape, *pose, *motion, ToFilter(context.Entities(), filter));
            if (!found)
            {
                return false;
            }
            *hit = Script::MoveCastHit{.body = Script::EntityValue(found->body.GetId()),
                                       .collider = Script::EntityValue(found->collider.GetId()),
                                       .point = found->point,
                                       .normal = found->normal,
                                       .distance = found->distance};
            return true;
        }

        /**
         * @brief Writes the colliders a shape overlaps to `hits` (PhysicsWorld::Overlap).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t Overlap(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                             const Transform* pose, const Script::QueryFilter* filter, Script::OverlapHit* hits,
                             std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || shape == nullptr || pose == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<OverlapHit> found = Buffer<OverlapHit>(capacity);
            const std::size_t count = world->Overlap(*shape, *pose, found, ToFilter(context.Entities(), filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits, ToScriptOverlap);
            return static_cast<std::int32_t>(count);
        }

        /**
         * @brief Writes the colliders a shape overlaps to `hits`, deepest first, with the way out of each
         * (PhysicsWorld::Penetrations).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t Penetrations(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const Script::QueryFilter* filter,
                                  Script::PenetrationHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || shape == nullptr || pose == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<PenetrationHit> found = Buffer<PenetrationHit>(capacity);
            const std::size_t count = world->Penetrations(*shape, *pose, found, ToFilter(context.Entities(), filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           [](const PenetrationHit& hit) {
                               return Script::PenetrationHit{.body = Script::EntityValue(hit.body.GetId()),
                                                             .collider = Script::EntityValue(hit.collider.GetId()),
                                                             .point = hit.point,
                                                             .normal = hit.normal,
                                                             .depth = hit.depth};
                           });
            return static_cast<std::int32_t>(count);
        }

        /**
         * @brief The offset that moves a shape out of everything it overlaps (PhysicsWorld::Depenetrate).
         * @param[out] offset The offset; zero when there is no world.
         * @return Whether there was a world to ask.
         */
        bool Depenetrate(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                         const Transform* pose, const Script::QueryFilter* filter, Math::Vec3* offset)
        {
            if (offset == nullptr)
            {
                return false;
            }
            *offset = Math::Vec3(0.0f);
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || shape == nullptr || pose == nullptr)
            {
                return false;
            }
            *offset = world->Depenetrate(*shape, *pose, ToFilter(context.Entities(), filter));
            return true;
        }

        /**
         * @brief Writes the colliders containing `point` to `hits` (PhysicsWorld::OverlapPoint).
         * @param hits Where to write at most `capacity` hits.
         * @return How many it wrote.
         */
        std::int32_t OverlapPoint(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* point,
                                  const Script::QueryFilter* filter, Script::OverlapHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || point == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<OverlapHit> found = Buffer<OverlapHit>(capacity);
            const std::size_t count = world->OverlapPoint(*point, found, ToFilter(context.Entities(), filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits, ToScriptOverlap);
            return static_cast<std::int32_t>(count);
        }

        /**
         * @brief Writes the contacts the body entity `body` has now to `contacts` (PhysicsWorld::ForEachContact).
         * @param contacts Where to write at most `capacity` contacts.
         * @return How many it wrote.
         */
        std::int32_t ContactsOf(const CallContext& context, PhysicsScripting& physics, Script::EntityId body,
                                Script::Contact* contacts, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || !physics.HasBody(context, body) || contacts == nullptr)
            {
                return 0;
            }
            std::int32_t count = 0;
            world->ForEachContact(context.Entities().HandleOf(body), [&](const Contact& contact) {
                if (count < capacity)
                {
                    contacts[count++] =
                        Script::Contact{.body = Script::EntityValue(contact.body.GetId()),
                                        .collider = Script::EntityValue(contact.collider.GetId()),
                                        .otherBody = Script::EntityValue(contact.otherBody.GetId()),
                                        .otherCollider = Script::EntityValue(contact.otherCollider.GetId()),
                                        .point = contact.point,
                                        .normal = contact.normal};
                }
            });
            return count;
        }

        /** @brief Whether the body of `body` is asleep: false with no world or no body. */
        bool IsSleeping(const CallContext& context, PhysicsScripting& physics, Script::EntityId body)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || !physics.HasBody(context, body))
            {
                return false;
            }
            return world->IsSleeping(context.Entities().HandleOf(body));
        }

        /**
         * @brief The world-space box around the body of `body` (PhysicsWorld::Bounds).
         * @param[out] bounds The box; the default when there is none.
         * @return Whether the entity has a body.
         */
        bool Bounds(const CallContext& context, PhysicsScripting& physics, Script::EntityId body, Math::AABB* bounds)
        {
            if (bounds == nullptr)
            {
                return false;
            }
            *bounds = Math::AABB{};
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr || !physics.HasBody(context, body))
            {
                return false;
            }
            const std::optional<Math::AABB> found = world->Bounds(context.Entities().HandleOf(body));
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
            if (gravity == nullptr)
            {
                return false;
            }
            *gravity = Math::Vec3(0.0f);
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
            if (world == nullptr || gravity == nullptr)
            {
                return false;
            }
            world->SetGravity(*gravity);
            return true;
        }

        /**
         * @brief Chooses the categories of debug lines the world draws: `categories` is DebugDraw's bits.
         * @return Whether there was a world.
         */
        bool SetDebugDraw(const CallContext& context, PhysicsScripting& physics, std::uint32_t categories)
        {
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return false;
            }
            world->SetDebugDraw(static_cast<DebugDraw>(categories & static_cast<std::uint32_t>(DebugDraw::All)));
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
            if (world == nullptr || (vertices == nullptr && vertexCount > 0) || (indices == nullptr && indexCount > 0))
            {
                return 0;
            }
            // The owner's meshes, which the world reaches, say why they refuse empty or broken data.
            const std::span<const Math::Vec3> vertexSpan(vertices, static_cast<std::size_t>(std::max(vertexCount, 0)));
            const std::span<const std::uint32_t> indexSpan(indices, static_cast<std::size_t>(std::max(indexCount, 0)));
            return world->CreateCollisionMesh(vertexSpan, indexSpan).id;
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
         * @param bodyB The empty value (uint.MaxValue in C#) is the world; any other names an entity of the active
         * scene.
         * @param[out] frames The joint's frames; identity frames when a body is not alive.
         */
        void FrameFromWorld(const CallContext& context, PhysicsScripting& physics, Script::EntityId bodyA,
                            Script::EntityId bodyB, const JointFrame* anchor, JointFrames* frames)
        {
            if (frames == nullptr)
            {
                return;
            }
            *frames = JointFrames{};
            if (anchor == nullptr)
            {
                return;
            }
            EntityList& entities = context.Entities();
            const bool aliveA = entities.HasEntity(bodyA);
            if (!aliveA || (bodyB != Entity::Null && !entities.HasEntity(bodyB)))
            {
                physics.WarnOnce(context, std::string(aliveA ? "bodyB" : "bodyA") +
                                              " is not alive, so it gives identity frames");
                return;
            }
            // Qualified: this function's own name hides FenrirPhysics's within this namespace.
            *frames = Fenrir::FrameFromWorld(entities.HandleOf(bodyA),
                                             bodyB == Entity::Null ? Entity() : entities.HandleOf(bodyB), *anchor);
        }

        /**
         * @brief The world's reading of the joint on `joint` (PhysicsWorld::GetJointState). An inactive or unstepped
         * joint reads zeroed from the world itself, which already warns about an inactive one.
         * @param[out] state The reading; the zeroed state (warning once) with no world or no Joint.
         */
        void GetJointState(const CallContext& context, PhysicsScripting& physics, Script::EntityId joint,
                           JointState* state)
        {
            if (state == nullptr)
            {
                return;
            }
            *state = JointState{};
            PhysicsWorld* world = physics.WorldOf(context);
            if (world == nullptr)
            {
                return;
            }
            EntityList& entities = context.Entities();
            if (!entities.HasEntity(joint) || !entities.GetEntity(joint).HasComponent<Joint>())
            {
                physics.WarnOnce(context, "the entity has no Joint, so it reads zeroed");
                return;
            }
            *state = world->GetJointState(entities.HandleOf(joint));
        }
    } // namespace PhysicsNatives

    ScriptingExtension PhysicsScriptingExtension()
    {
        ScriptingExtension extension;
        extension.name = "FenrirPhysics";
        extension.SetState(std::make_shared<PhysicsScripting>());
        extension.sharedAssemblies = {"Fenrir.Physics.dll"};
        Generated::RegisterPhysics(extension);
        return extension;
    }
} // namespace Fenrir
