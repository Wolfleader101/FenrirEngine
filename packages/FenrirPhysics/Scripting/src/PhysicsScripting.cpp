#include "FenrirPhysicsScripting/PhysicsScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "ScriptQueries.hpp"

// How the physics components, events and shared structs look to C# is generated (api/FenrirPhysics.binding.toml): their
// mappings, checked against the Fenrir.Physics contract whenever a script assembly loads, and this registration of
// them.
namespace Fenrir::Generated
{
    void RegisterPhysics(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    namespace
    {
        // The extension's state, which every native is given: what they share, made by PhysicsScriptingExtension and
        // kept by the Scripting given the extension.
        struct PhysicsScripting
        {
            // A warning logged once for each place in this file that notices a call doing nothing, in debug builds
            // only.
            void WarnOnce(const CallContext& context, const std::string& message,
                          std::source_location where = std::source_location::current())
            {
#ifndef NDEBUG
                // Keyed by the message too: one call site can notice more than one reason (no world, then no body).
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                    {
                        return;
                    }
                }
                context.GetApp().Logger()->Warn("[physics scripting] {} (warned once)", message);
#else
                (void)context;
                (void)message;
                (void)where;
#endif
            }

            // The active scene's physics world, or null (warning once for the caller's call site).
            PhysicsWorld* WorldOf(const CallContext& context, std::string_view function,
                                  std::source_location where = std::source_location::current())
            {
                PhysicsWorld* world = PhysicsWorld::Of(context.GetApp().GetActiveScene());
                if (world == nullptr)
                {
                    WarnOnce(context,
                             std::string(function) + ": the active scene has no physics world yet, so it does nothing",
                             where);
                }
                return world;
            }

            // Whether `entity` is alive with a body (a RigidBody, or a Collider standing for its body, a character's
            // included); warns once if not.
            bool HasBody(const CallContext& context, std::uint32_t entity, std::string_view function,
                         std::source_location where = std::source_location::current())
            {
                EntityList& entities = context.Entities();
                if (entities.HasEntity(entity))
                {
                    const Entity body = entities.GetEntity(entity);
                    if (body.HasComponent<RigidBody>() || body.HasComponent<Collider>())
                    {
                        return true;
                    }
                }
                WarnOnce(context, std::string(function) + ": the entity has no body, so it does nothing", where);
                return false;
            }

            // The calls warned about, by where and why. Natives may run on several threads at once.
            std::mutex mutex;
            std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
        };

        // A force-like function: queues `apply` on the entity's body in the active scene's world.
        template <void (PhysicsWorld::*Apply)(const Entity&, const Math::Vec3&)>
        bool Push(const CallContext& context, PhysicsScripting& physics, std::uint32_t entity, const Math::Vec3* amount,
                  std::string_view function, std::source_location where = std::source_location::current())
        {
            PhysicsWorld* world = physics.WorldOf(context, function, where);
            if (world == nullptr || !physics.HasBody(context, entity, function, where) || amount == nullptr)
            {
                return false;
            }
            (world->*Apply)(context.Entities().HandleOf(entity), *amount);
            return true;
        }

        bool AddForce(const CallContext& context, PhysicsScripting& physics, std::uint32_t entity,
                      const Math::Vec3* force)
        {
            return Push<&PhysicsWorld::AddForce>(context, physics, entity, force, "AddForce");
        }

        bool AddImpulse(const CallContext& context, PhysicsScripting& physics, std::uint32_t entity,
                        const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddImpulse>(context, physics, entity, impulse, "AddImpulse");
        }

        bool AddTorque(const CallContext& context, PhysicsScripting& physics, std::uint32_t entity,
                       const Math::Vec3* torque)
        {
            return Push<&PhysicsWorld::AddTorque>(context, physics, entity, torque, "AddTorque");
        }

        bool AddAngularImpulse(const CallContext& context, PhysicsScripting& physics, std::uint32_t entity,
                               const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddAngularImpulse>(context, physics, entity, impulse, "AddAngularImpulse");
        }

        // The query types as C# holds them, with each entity as its 32-bit value (ScriptQueries.hpp), which the natives
        // convert to and from FenrirPhysics's own.
        namespace Script = ScriptQueries;

        QueryFilter ToFilter(const CallContext& context, const Script::QueryFilter* filter)
        {
            if (filter == nullptr)
            {
                return {};
            }
            return QueryFilter{.mask = filter->mask,
                               .layers = filter->layers,
                               .ignoreBody = filter->ignoreBody.value == Entity::Null
                                                 ? Entity()
                                                 : context.Entities().HandleOf(filter->ignoreBody.value),
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

        bool Raycast(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                     const Math::Vec3* direction, float maxDistance, const Script::QueryFilter* filter,
                     Script::RaycastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Script::RaycastHit{};
            PhysicsWorld* world = physics.WorldOf(context, "Raycast");
            if (world == nullptr || origin == nullptr || direction == nullptr)
            {
                return false;
            }
            const std::optional<RaycastHit> found =
                world->Raycast(*origin, *direction, maxDistance, ToFilter(context, filter));
            if (!found)
            {
                return false;
            }
            *hit = ToScript<Script::RaycastHit>(*found);
            return true;
        }

        std::int32_t RaycastAll(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                                const Math::Vec3* direction, float maxDistance, const Script::QueryFilter* filter,
                                Script::RaycastHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "RaycastAll");
            if (world == nullptr || origin == nullptr || direction == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<RaycastHit> found = Buffer<RaycastHit>(capacity);
            const std::size_t count =
                world->RaycastAll(*origin, *direction, maxDistance, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           ToScript<Script::RaycastHit, RaycastHit>);
            return static_cast<std::int32_t>(count);
        }

        Script::OverlapHit ToScriptOverlap(const OverlapHit& hit)
        {
            return Script::OverlapHit{.body = Script::EntityValue(hit.body.GetId()),
                                      .collider = Script::EntityValue(hit.collider.GetId())};
        }

        bool ShapeCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                       const Transform* pose, const Math::Vec3* direction, float maxDistance,
                       const Script::QueryFilter* filter, Script::ShapeCastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Script::ShapeCastHit{};
            PhysicsWorld* world = physics.WorldOf(context, "ShapeCast");
            if (world == nullptr || shape == nullptr || pose == nullptr || direction == nullptr)
            {
                return false;
            }
            const std::optional<ShapeCastHit> found =
                world->ShapeCast(*shape, *pose, *direction, maxDistance, ToFilter(context, filter));
            if (!found)
            {
                return false;
            }
            *hit = ToScript<Script::ShapeCastHit>(*found);
            return true;
        }

        std::int32_t ShapeCastAll(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const Math::Vec3* direction, float maxDistance,
                                  const Script::QueryFilter* filter, Script::ShapeCastHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "ShapeCastAll");
            if (world == nullptr || shape == nullptr || pose == nullptr || direction == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<ShapeCastHit> found = Buffer<ShapeCastHit>(capacity);
            const std::size_t count =
                world->ShapeCastAll(*shape, *pose, *direction, maxDistance, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           ToScript<Script::ShapeCastHit, ShapeCastHit>);
            return static_cast<std::int32_t>(count);
        }

        bool MoveCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                      const Transform* pose, const Math::Vec3* motion, const Script::QueryFilter* filter,
                      Script::MoveCastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = Script::MoveCastHit{};
            PhysicsWorld* world = physics.WorldOf(context, "MoveCast");
            if (world == nullptr || shape == nullptr || pose == nullptr || motion == nullptr)
            {
                return false;
            }
            const std::optional<MoveCastHit> found = world->MoveCast(*shape, *pose, *motion, ToFilter(context, filter));
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

        std::int32_t Overlap(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                             const Transform* pose, const Script::QueryFilter* filter, Script::OverlapHit* hits,
                             std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "Overlap");
            if (world == nullptr || shape == nullptr || pose == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<OverlapHit> found = Buffer<OverlapHit>(capacity);
            const std::size_t count = world->Overlap(*shape, *pose, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits, ToScriptOverlap);
            return static_cast<std::int32_t>(count);
        }

        std::int32_t Penetrations(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const Script::QueryFilter* filter,
                                  Script::PenetrationHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "Penetrations");
            if (world == nullptr || shape == nullptr || pose == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<PenetrationHit> found = Buffer<PenetrationHit>(capacity);
            const std::size_t count = world->Penetrations(*shape, *pose, found, ToFilter(context, filter));
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

        bool Depenetrate(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                         const Transform* pose, const Script::QueryFilter* filter, Math::Vec3* offset)
        {
            if (offset == nullptr)
            {
                return false;
            }
            *offset = Math::Vec3(0.0f);
            PhysicsWorld* world = physics.WorldOf(context, "Depenetrate");
            if (world == nullptr || shape == nullptr || pose == nullptr)
            {
                return false;
            }
            *offset = world->Depenetrate(*shape, *pose, ToFilter(context, filter));
            return true;
        }

        std::int32_t OverlapPoint(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* point,
                                  const Script::QueryFilter* filter, Script::OverlapHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "OverlapPoint");
            if (world == nullptr || point == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<OverlapHit> found = Buffer<OverlapHit>(capacity);
            const std::size_t count = world->OverlapPoint(*point, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits, ToScriptOverlap);
            return static_cast<std::int32_t>(count);
        }

        std::int32_t ContactsOf(const CallContext& context, PhysicsScripting& physics, std::uint32_t body,
                                Script::Contact* contacts, std::int32_t capacity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "ContactsOf");
            if (world == nullptr || !physics.HasBody(context, body, "ContactsOf") || contacts == nullptr)
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

        bool IsSleeping(const CallContext& context, PhysicsScripting& physics, std::uint32_t body)
        {
            PhysicsWorld* world = physics.WorldOf(context, "IsSleeping");
            if (world == nullptr || !physics.HasBody(context, body, "IsSleeping"))
            {
                return false;
            }
            return world->IsSleeping(context.Entities().HandleOf(body));
        }

        bool Bounds(const CallContext& context, PhysicsScripting& physics, std::uint32_t body, Math::AABB* bounds)
        {
            if (bounds == nullptr)
            {
                return false;
            }
            *bounds = Math::AABB{};
            PhysicsWorld* world = physics.WorldOf(context, "Bounds");
            if (world == nullptr || !physics.HasBody(context, body, "Bounds"))
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

        bool Gravity(const CallContext& context, PhysicsScripting& physics, Math::Vec3* gravity)
        {
            if (gravity == nullptr)
            {
                return false;
            }
            *gravity = Math::Vec3(0.0f);
            PhysicsWorld* world = physics.WorldOf(context, "Gravity");
            if (world == nullptr)
            {
                return false;
            }
            *gravity = world->Gravity();
            return true;
        }

        bool SetGravity(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* gravity)
        {
            PhysicsWorld* world = physics.WorldOf(context, "SetGravity");
            if (world == nullptr || gravity == nullptr)
            {
                return false;
            }
            world->SetGravity(*gravity);
            return true;
        }

        bool SetDebugDraw(const CallContext& context, PhysicsScripting& physics, std::uint32_t categories)
        {
            PhysicsWorld* world = physics.WorldOf(context, "SetDebugDraw");
            if (world == nullptr)
            {
                return false;
            }
            world->SetDebugDraw(static_cast<DebugDraw>(categories & static_cast<std::uint32_t>(DebugDraw::All)));
            return true;
        }

        std::uint64_t CreateMesh(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* vertices,
                                 std::int32_t vertexCount, const std::uint32_t* indices, std::int32_t indexCount)
        {
            PhysicsWorld* world = physics.WorldOf(context, "CreateCollisionMesh");
            if (world == nullptr || (vertices == nullptr && vertexCount > 0) || (indices == nullptr && indexCount > 0))
            {
                return 0;
            }
            // The owner's meshes, which the world reaches, say why they refuse empty or broken data.
            const std::span<const Math::Vec3> vertexSpan(vertices, static_cast<std::size_t>(std::max(vertexCount, 0)));
            const std::span<const std::uint32_t> indexSpan(indices, static_cast<std::size_t>(std::max(indexCount, 0)));
            return world->CreateCollisionMesh(vertexSpan, indexSpan).id;
        }

        bool ReleaseMesh(const CallContext& context, PhysicsScripting& physics, std::uint64_t mesh)
        {
            PhysicsWorld* world = physics.WorldOf(context, "ReleaseCollisionMesh");
            if (world == nullptr)
            {
                return false;
            }
            // Releasing a mesh that is not alive warns there, every time, as it does in C++.
            return world->ReleaseCollisionMesh(CollisionMesh{mesh});
        }

        static_assert(sizeof(JointFrames) == 2 * sizeof(JointFrame),
                      "Keep JointFrames as the C# JointFrames lays it out.");

        // C++'s FrameFromWorld, once the bodies it needs are alive (C++'s gives identity frames without a word, having
        // no app to warn through); identity frames, warning once, if not. The empty value is the empty Entity, the
        // world, as bodyB; any other value names an entity of the active scene. Needs no physics world.
        void FrameFromWorldNative(const CallContext& context, PhysicsScripting& physics, std::uint32_t bodyA,
                                  std::uint32_t bodyB, const JointFrame* anchor, JointFrames* frames)
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
                physics.WarnOnce(context, std::string("FrameFromWorld: ") + (aliveA ? "bodyB" : "bodyA") +
                                              " is not alive, so it gives identity frames");
                return;
            }
            *frames = FrameFromWorld(entities.HandleOf(bodyA),
                                     bodyB == Entity::Null ? Entity() : entities.HandleOf(bodyB), *anchor);
        }

        // The world's reading of the joint, or the zeroed state (warning once) with no world or no Joint. An inactive
        // or unstepped joint reads zeroed from the world itself, which already warns about an inactive one.
        void GetJointState(const CallContext& context, PhysicsScripting& physics, std::uint32_t joint,
                           JointState* state)
        {
            if (state == nullptr)
            {
                return;
            }
            *state = JointState{};
            PhysicsWorld* world = physics.WorldOf(context, "GetJointState");
            if (world == nullptr)
            {
                return;
            }
            EntityList& entities = context.Entities();
            if (!entities.HasEntity(joint) || !entities.GetEntity(joint).HasComponent<Joint>())
            {
                physics.WarnOnce(context, "GetJointState: the entity has no Joint, so it reads zeroed");
                return;
            }
            *state = world->GetJointState(entities.HandleOf(joint));
        }
    } // namespace

    ScriptingExtension PhysicsScriptingExtension()
    {
        const std::string prefix = "Fenrir.Physics.PhysicsNative.";
        ScriptingExtension extension;
        extension.name = "FenrirPhysics";
        extension.SetState(std::make_shared<PhysicsScripting>());
        extension.sharedAssemblies = {"Fenrir.Physics.dll"};
        extension.functions = {
            NativeFunction::Make<&AddForce>(prefix + "AddForce"),
            NativeFunction::Make<&AddImpulse>(prefix + "AddImpulse"),
            NativeFunction::Make<&AddTorque>(prefix + "AddTorque"),
            NativeFunction::Make<&AddAngularImpulse>(prefix + "AddAngularImpulse"),
            NativeFunction::Make<&Raycast>(prefix + "Raycast"),
            NativeFunction::Make<&RaycastAll>(prefix + "RaycastAll"),
            NativeFunction::Make<&ShapeCast>(prefix + "ShapeCast"),
            NativeFunction::Make<&ShapeCastAll>(prefix + "ShapeCastAll"),
            NativeFunction::Make<&MoveCast>(prefix + "MoveCast"),
            NativeFunction::Make<&Overlap>(prefix + "Overlap"),
            NativeFunction::Make<&Penetrations>(prefix + "Penetrations"),
            NativeFunction::Make<&Depenetrate>(prefix + "Depenetrate"),
            NativeFunction::Make<&OverlapPoint>(prefix + "OverlapPoint"),
            NativeFunction::Make<&ContactsOf>(prefix + "ContactsOf"),
            NativeFunction::Make<&IsSleeping>(prefix + "IsSleeping"),
            NativeFunction::Make<&Bounds>(prefix + "Bounds"),
            NativeFunction::Make<&Gravity>(prefix + "Gravity"),
            NativeFunction::Make<&SetGravity>(prefix + "SetGravity"),
            NativeFunction::Make<&SetDebugDraw>(prefix + "SetDebugDraw"),
            NativeFunction::Make<&CreateMesh>(prefix + "CreateCollisionMesh"),
            NativeFunction::Make<&ReleaseMesh>(prefix + "ReleaseCollisionMesh"),
            NativeFunction::Make<&FrameFromWorldNative>(prefix + "FrameFromWorld"),
            NativeFunction::Make<&GetJointState>(prefix + "GetJointState"),
        };
        Generated::RegisterPhysics(extension);
        return extension;
    }
} // namespace Fenrir
