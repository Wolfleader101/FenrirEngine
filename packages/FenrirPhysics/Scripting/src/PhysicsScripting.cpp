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
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "PhysicsState.hpp"

// How the physics components and events look to C#: each mapping is checked against its mirror in the Fenrir.Physics
// contract (Managed/Fenrir.Physics) whenever a script assembly loads, so a drift fails the load and names the field.

template <>
struct Iris::EnumMapping<Fenrir::MotionType>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.MotionType";
};

template <>
struct Iris::EnumMapping<Fenrir::ShapeType>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.ShapeType";
};

template <>
struct Iris::StructMapping<Fenrir::RigidBody>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.RigidBody";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Fenrir::RigidBody, motionType, "MotionType"), IRIS_FIELD_AS(Fenrir::RigidBody, mass, "Mass"),
        IRIS_FIELD_AS(Fenrir::RigidBody, linearDamping, "LinearDamping"),
        IRIS_FIELD_AS(Fenrir::RigidBody, angularDamping, "AngularDamping"),
        IRIS_FIELD_AS(Fenrir::RigidBody, gravityScale, "GravityScale"),
        IRIS_FIELD_AS(Fenrir::RigidBody, linearVelocity, "LinearVelocity"),
        IRIS_FIELD_AS(Fenrir::RigidBody, angularVelocity, "AngularVelocity"),
        IRIS_FIELD_AS(Fenrir::RigidBody, interpolate, "Interpolate"));
};

template <>
struct Iris::StructMapping<Fenrir::BoxShape>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.BoxShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::BoxShape, halfExtents, "HalfExtents"));
};

template <>
struct Iris::StructMapping<Fenrir::SphereShape>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.SphereShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::SphereShape, radius, "Radius"));
};

template <>
struct Iris::StructMapping<Fenrir::CapsuleShape>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.CapsuleShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::CapsuleShape, halfHeight, "HalfHeight"),
                                                      IRIS_FIELD_AS(Fenrir::CapsuleShape, radius, "Radius"));
};

template <>
struct Iris::StructMapping<Fenrir::CollisionMesh>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.CollisionMesh";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::CollisionMesh, id, "Id"));
};

template <>
struct Iris::StructMapping<Fenrir::ConvexHullShape>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.ConvexHullShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::ConvexHullShape, hull, "Hull"));
};

template <>
struct Iris::StructMapping<Fenrir::MeshShape>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.MeshShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::MeshShape, mesh, "Mesh"));
};

namespace Fenrir::Detail
{
    // CollisionShape keeps its tag and union private, and FenrirPhysics does not know Iris, so these stand in for
    // CollisionShape and Collider: the same members in the same order (a one-byte tag, then the union at its own
    // alignment), nameable by a mapping. MakeComponent checks the size and alignment; the scripting tests read and
    // write real colliders through them.
    struct CollisionShapeLayout
    {
        ShapeType type = ShapeType::Box;
        union {
            BoxShape box{};
            SphereShape sphere;
            CapsuleShape capsule;
            ConvexHullShape hull;
            MeshShape mesh;
        };
    };
    static_assert(sizeof(CollisionShapeLayout) == sizeof(CollisionShape) &&
                      alignof(CollisionShapeLayout) == alignof(CollisionShape),
                  "CollisionShape changed shape: update CollisionShapeLayout and the C# CollisionShape.");

    struct ColliderLayout
    {
        CollisionShapeLayout shape;
    };
} // namespace Fenrir::Detail

// The tag and every kind, in declaration order, each kind at the union's offset (the C# explicit layout mirrors it).
template <>
struct Iris::StructMapping<Fenrir::Detail::CollisionShapeLayout>
{
    using Layout = Fenrir::Detail::CollisionShapeLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.CollisionShape";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, box, "_box"),
                           IRIS_FIELD_AS(Layout, sphere, "_sphere"), IRIS_FIELD_AS(Layout, capsule, "_capsule"),
                           IRIS_FIELD_AS(Layout, hull, "_hull"), IRIS_FIELD_AS(Layout, mesh, "_mesh"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ColliderLayout>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.Collider";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::ColliderLayout, shape, "Shape"));
};

template <>
struct Iris::StructMapping<Fenrir::PhysicsMaterial>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.PhysicsMaterial";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::PhysicsMaterial, friction, "Friction"),
                           IRIS_FIELD_AS(Fenrir::PhysicsMaterial, restitution, "Restitution"));
};

template <>
struct Iris::StructMapping<Fenrir::CollisionFilter>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.CollisionFilter";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::CollisionFilter, layers, "Layers"),
                                                      IRIS_FIELD_AS(Fenrir::CollisionFilter, mask, "Mask"));
};

template <>
struct Iris::StructMapping<Fenrir::Trigger>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.Trigger";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Trigger, enabled, "Enabled"));
};

template <>
struct Iris::StructMapping<Fenrir::ContactEvents>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.ContactEvents";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::ContactEvents, enabled, "Enabled"));
};

namespace Fenrir::Detail
{
    // The events hold Entities, which Iris cannot map, so these stand in for them: the same members in the same order,
    // each Entity as the link the engine stores (as for Relationship). MakeEvent checks the size and alignment; the
    // scripting tests read real events through them.
    struct ContactBeginLayout
    {
        EntityLinkView bodyA;
        EntityLinkView bodyB;
        EntityLinkView colliderA;
        EntityLinkView colliderB;
        Math::Vec3 point;
        Math::Vec3 normal;
        float relativeSpeed;
    };

    struct ContactEndLayout
    {
        EntityLinkView bodyA;
        EntityLinkView bodyB;
        EntityLinkView colliderA;
        EntityLinkView colliderB;
    };

    // TriggerEnter and TriggerExit have the same members, but each is its own C# struct, so each has its own stand-in.
    struct TriggerEnterLayout
    {
        EntityLinkView trigger;
        EntityLinkView otherBody;
        EntityLinkView otherCollider;
    };

    struct TriggerExitLayout
    {
        EntityLinkView trigger;
        EntityLinkView otherBody;
        EntityLinkView otherCollider;
    };
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::ContactBeginLayout>
{
    using Layout = Fenrir::Detail::ContactBeginLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.ContactBegin";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, bodyA, "BodyA"), IRIS_FIELD_AS(Layout, bodyB, "BodyB"),
                           IRIS_FIELD_AS(Layout, colliderA, "ColliderA"), IRIS_FIELD_AS(Layout, colliderB, "ColliderB"),
                           IRIS_FIELD_AS(Layout, point, "Point"), IRIS_FIELD_AS(Layout, normal, "Normal"),
                           IRIS_FIELD_AS(Layout, relativeSpeed, "RelativeSpeed"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ContactEndLayout>
{
    using Layout = Fenrir::Detail::ContactEndLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.ContactEnd";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, bodyA, "BodyA"), IRIS_FIELD_AS(Layout, bodyB, "BodyB"),
        IRIS_FIELD_AS(Layout, colliderA, "ColliderA"), IRIS_FIELD_AS(Layout, colliderB, "ColliderB"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::TriggerEnterLayout>
{
    using Layout = Fenrir::Detail::TriggerEnterLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.TriggerEnter";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, trigger, "Trigger"), IRIS_FIELD_AS(Layout, otherBody, "OtherBody"),
                           IRIS_FIELD_AS(Layout, otherCollider, "OtherCollider"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::TriggerExitLayout>
{
    using Layout = Fenrir::Detail::TriggerExitLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.TriggerExit";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, trigger, "Trigger"), IRIS_FIELD_AS(Layout, otherBody, "OtherBody"),
                           IRIS_FIELD_AS(Layout, otherCollider, "OtherCollider"));
};

namespace Fenrir
{
    namespace
    {
        // The owner whose extension was made last. Native functions are plain functions, so this is how the collision
        // mesh functions reach the owner's meshes. Weak: an owner destroyed while scripting runs is simply gone.
        std::mutex ownerMutex;
        std::weak_ptr<Physics::State> scriptedOwner;

        // A warning logged once for each place in this file that notices a call doing nothing, in debug builds only.
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason (no world, then no body).
            static std::mutex mutex;
            static std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
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
        PhysicsWorld* WorldFor(const CallContext& context, std::string_view function,
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

        // Whether `entity` is alive with a body (a RigidBody, or a Collider standing for its body); warns once if not.
        bool HasBody(const CallContext& context, std::uint32_t entity, std::string_view function,
                     std::source_location where = std::source_location::current())
        {
            EntityList& entities = context.Entities();
            if (entities.HasEntity(entity) && (entities.Has(ComponentType::Native<RigidBody>(), entity) ||
                                               entities.Has(ComponentType::Native<Collider>(), entity)))
            {
                return true;
            }
            WarnOnce(context, std::string(function) + ": the entity has no body, so it does nothing", where);
            return false;
        }

        // A force-like function: queues `apply` on the entity's body in the active scene's world.
        template <void (PhysicsWorld::*Apply)(const Entity&, const Math::Vec3&)>
        bool Push(const CallContext& context, std::uint32_t entity, const Math::Vec3* amount, std::string_view function,
                  std::source_location where = std::source_location::current())
        {
            PhysicsWorld* world = WorldFor(context, function, where);
            if (world == nullptr || !HasBody(context, entity, function, where) || amount == nullptr)
            {
                return false;
            }
            (world->*Apply)(context.Entities().HandleOf(entity), *amount);
            return true;
        }

        bool AddForce(const CallContext& context, std::uint32_t entity, const Math::Vec3* force)
        {
            return Push<&PhysicsWorld::AddForce>(context, entity, force, "AddForce");
        }

        bool AddImpulse(const CallContext& context, std::uint32_t entity, const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddImpulse>(context, entity, impulse, "AddImpulse");
        }

        bool AddTorque(const CallContext& context, std::uint32_t entity, const Math::Vec3* torque)
        {
            return Push<&PhysicsWorld::AddTorque>(context, entity, torque, "AddTorque");
        }

        bool AddAngularImpulse(const CallContext& context, std::uint32_t entity, const Math::Vec3* impulse)
        {
            return Push<&PhysicsWorld::AddAngularImpulse>(context, entity, impulse, "AddAngularImpulse");
        }

        // The C# mirrors of the query types (Managed/Fenrir.Physics/Queries.cs), with entities as their 32-bit values,
        // since a Fenrir::Entity holds a pointer to its list. They cross only by pointer, so nothing checks them at
        // load: the sizes are checked here and the scripting tests read every field back.
        struct ScriptQueryFilter
        {
            std::uint32_t mask;
            std::uint32_t ignoreBody;
            bool hitTriggers;
        };
        static_assert(sizeof(ScriptQueryFilter) == 12, "Keep ScriptQueryFilter as the C# QueryFilter lays it out.");

        // RaycastHit and ShapeCastHit, which have the same fields.
        struct ScriptHit
        {
            std::uint32_t body;
            std::uint32_t collider;
            Math::Vec3 point;
            Math::Vec3 normal;
            float distance;
        };
        static_assert(sizeof(ScriptHit) == 36, "Keep ScriptHit as the C# RaycastHit and ShapeCastHit lay it out.");

        QueryFilter ToFilter(const CallContext& context, const ScriptQueryFilter* filter)
        {
            if (filter == nullptr)
            {
                return {};
            }
            return QueryFilter{.mask = filter->mask,
                               .ignoreBody = filter->ignoreBody == Entity::Null
                                                 ? Entity()
                                                 : context.Entities().HandleOf(filter->ignoreBody),
                               .hitTriggers = filter->hitTriggers};
        }

        template <typename Hit>
        ScriptHit ToScript(const Hit& hit)
        {
            return ScriptHit{.body = hit.body.GetId(),
                             .collider = hit.collider.GetId(),
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

        bool Raycast(const CallContext& context, const Math::Vec3* origin, const Math::Vec3* direction,
                     float maxDistance, const ScriptQueryFilter* filter, ScriptHit* hit)
        {
            PhysicsWorld* world = WorldFor(context, "Raycast");
            if (world == nullptr || origin == nullptr || direction == nullptr || hit == nullptr)
            {
                return false;
            }
            const std::optional<RaycastHit> found =
                world->Raycast(*origin, *direction, maxDistance, ToFilter(context, filter));
            if (!found)
            {
                return false;
            }
            *hit = ToScript(*found);
            return true;
        }

        std::int32_t RaycastAll(const CallContext& context, const Math::Vec3* origin, const Math::Vec3* direction,
                                float maxDistance, const ScriptQueryFilter* filter, ScriptHit* hits,
                                std::int32_t capacity)
        {
            PhysicsWorld* world = WorldFor(context, "RaycastAll");
            if (world == nullptr || origin == nullptr || direction == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<RaycastHit> found = Buffer<RaycastHit>(capacity);
            const std::size_t count =
                world->RaycastAll(*origin, *direction, maxDistance, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           ToScript<RaycastHit>);
            return static_cast<std::int32_t>(count);
        }

        struct ScriptOverlapHit
        {
            std::uint32_t body;
            std::uint32_t collider;
        };
        static_assert(sizeof(ScriptOverlapHit) == 8, "Keep ScriptOverlapHit as the C# OverlapHit lays it out.");

        ScriptOverlapHit ToScriptOverlap(const OverlapHit& hit)
        {
            return ScriptOverlapHit{.body = hit.body.GetId(), .collider = hit.collider.GetId()};
        }

        bool ShapeCast(const CallContext& context, const CollisionShape* shape, const Transform* pose,
                       const Math::Vec3* direction, float maxDistance, const ScriptQueryFilter* filter, ScriptHit* hit)
        {
            PhysicsWorld* world = WorldFor(context, "ShapeCast");
            if (world == nullptr || shape == nullptr || pose == nullptr || direction == nullptr || hit == nullptr)
            {
                return false;
            }
            const std::optional<ShapeCastHit> found =
                world->ShapeCast(*shape, *pose, *direction, maxDistance, ToFilter(context, filter));
            if (!found)
            {
                return false;
            }
            *hit = ToScript(*found);
            return true;
        }

        std::int32_t ShapeCastAll(const CallContext& context, const CollisionShape* shape, const Transform* pose,
                                  const Math::Vec3* direction, float maxDistance, const ScriptQueryFilter* filter,
                                  ScriptHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = WorldFor(context, "ShapeCastAll");
            if (world == nullptr || shape == nullptr || pose == nullptr || direction == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<ShapeCastHit> found = Buffer<ShapeCastHit>(capacity);
            const std::size_t count =
                world->ShapeCastAll(*shape, *pose, *direction, maxDistance, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits,
                           ToScript<ShapeCastHit>);
            return static_cast<std::int32_t>(count);
        }

        std::int32_t Overlap(const CallContext& context, const CollisionShape* shape, const Transform* pose,
                             const ScriptQueryFilter* filter, ScriptOverlapHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = WorldFor(context, "Overlap");
            if (world == nullptr || shape == nullptr || pose == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<OverlapHit> found = Buffer<OverlapHit>(capacity);
            const std::size_t count = world->Overlap(*shape, *pose, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits, ToScriptOverlap);
            return static_cast<std::int32_t>(count);
        }

        std::int32_t OverlapPoint(const CallContext& context, const Math::Vec3* point, const ScriptQueryFilter* filter,
                                  ScriptOverlapHit* hits, std::int32_t capacity)
        {
            PhysicsWorld* world = WorldFor(context, "OverlapPoint");
            if (world == nullptr || point == nullptr || hits == nullptr)
            {
                return 0;
            }
            std::vector<OverlapHit> found = Buffer<OverlapHit>(capacity);
            const std::size_t count = world->OverlapPoint(*point, found, ToFilter(context, filter));
            std::transform(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(count), hits, ToScriptOverlap);
            return static_cast<std::int32_t>(count);
        }

        struct ScriptContact
        {
            std::uint32_t body;
            std::uint32_t collider;
            std::uint32_t otherBody;
            std::uint32_t otherCollider;
            Math::Vec3 point;
            Math::Vec3 normal;
        };
        static_assert(sizeof(ScriptContact) == 40, "Keep ScriptContact as the C# Contact lays it out.");

        struct ScriptAabb
        {
            Math::Vec3 min;
            Math::Vec3 max;
        };
        static_assert(sizeof(ScriptAabb) == 24, "Keep ScriptAabb as the C# Aabb lays it out.");

        std::int32_t ContactsOf(const CallContext& context, std::uint32_t body, ScriptContact* contacts,
                                std::int32_t capacity)
        {
            PhysicsWorld* world = WorldFor(context, "ContactsOf");
            if (world == nullptr || !HasBody(context, body, "ContactsOf") || contacts == nullptr)
            {
                return 0;
            }
            std::int32_t count = 0;
            world->ForEachContact(context.Entities().HandleOf(body), [&](const Contact& contact) {
                if (count < capacity)
                {
                    contacts[count++] = ScriptContact{.body = contact.body.GetId(),
                                                      .collider = contact.collider.GetId(),
                                                      .otherBody = contact.otherBody.GetId(),
                                                      .otherCollider = contact.otherCollider.GetId(),
                                                      .point = contact.point,
                                                      .normal = contact.normal};
                }
            });
            return count;
        }

        bool IsSleeping(const CallContext& context, std::uint32_t body)
        {
            PhysicsWorld* world = WorldFor(context, "IsSleeping");
            if (world == nullptr || !HasBody(context, body, "IsSleeping"))
            {
                return false;
            }
            return world->IsSleeping(context.Entities().HandleOf(body));
        }

        bool Bounds(const CallContext& context, std::uint32_t body, ScriptAabb* bounds)
        {
            PhysicsWorld* world = WorldFor(context, "Bounds");
            if (world == nullptr || !HasBody(context, body, "Bounds") || bounds == nullptr)
            {
                return false;
            }
            const std::optional<Math::AABB> found = world->Bounds(context.Entities().HandleOf(body));
            if (!found)
            {
                return false;
            }
            *bounds = ScriptAabb{.min = found->min, .max = found->max};
            return true;
        }

        bool Gravity(const CallContext& context, Math::Vec3* gravity)
        {
            PhysicsWorld* world = WorldFor(context, "Gravity");
            if (world == nullptr || gravity == nullptr)
            {
                return false;
            }
            *gravity = world->Gravity();
            return true;
        }

        bool SetGravity(const CallContext& context, const Math::Vec3* gravity)
        {
            PhysicsWorld* world = WorldFor(context, "SetGravity");
            if (world == nullptr || gravity == nullptr)
            {
                return false;
            }
            world->SetGravity(*gravity);
            return true;
        }

        bool SetDebugDraw(const CallContext& context, std::uint32_t categories)
        {
            PhysicsWorld* world = WorldFor(context, "SetDebugDraw");
            if (world == nullptr)
            {
                return false;
            }
            world->SetDebugDraw(static_cast<DebugDraw>(categories & static_cast<std::uint32_t>(DebugDraw::All)));
            return true;
        }

        // The owner whose extension the running scripts were given, if it is alive and serves this App; warns once if
        // not.
        std::shared_ptr<Physics::State> OwnerFor(const CallContext& context, std::string_view function,
                                                 std::source_location where = std::source_location::current())
        {
            std::shared_ptr<Physics::State> owner;
            {
                const std::lock_guard<std::mutex> lock(ownerMutex);
                owner = scriptedOwner.lock();
            }
            if (owner == nullptr || owner->app != &context.GetApp())
            {
                WarnOnce(context, std::string(function) + ": the Physics owner is gone, so it does nothing", where);
                return nullptr;
            }
            return owner;
        }

        std::uint64_t CreateMesh(const CallContext& context, const Math::Vec3* vertices, std::int32_t vertexCount,
                                 const std::uint32_t* indices, std::int32_t indexCount)
        {
            const std::shared_ptr<Physics::State> owner = OwnerFor(context, "CreateCollisionMesh");
            if (owner == nullptr || (vertices == nullptr && vertexCount > 0) || (indices == nullptr && indexCount > 0))
            {
                return 0;
            }
            // The owner's meshes say why they refuse empty or broken data.
            const std::span<const Math::Vec3> vertexSpan(vertices, static_cast<std::size_t>(std::max(vertexCount, 0)));
            const std::span<const std::uint32_t> indexSpan(indices, static_cast<std::size_t>(std::max(indexCount, 0)));
            return owner->meshes.Create(vertexSpan, indexSpan).id;
        }

        bool ReleaseMesh(const CallContext& context, std::uint64_t mesh)
        {
            const std::shared_ptr<Physics::State> owner = OwnerFor(context, "ReleaseCollisionMesh");
            if (owner == nullptr)
            {
                return false;
            }
            // Releasing a mesh that is not alive warns there, every time, as it does in C++.
            return owner->meshes.Release(CollisionMesh{mesh});
        }
    } // namespace

    Fenrir::ScriptingExtension Physics::ScriptingExtension() const
    {
        {
            const std::lock_guard<std::mutex> lock(ownerMutex);
            scriptedOwner = m_state;
        }
        const std::string prefix = "Fenrir.Physics.PhysicsNative.";
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirPhysics";
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
            NativeFunction::Make<&Overlap>(prefix + "Overlap"),
            NativeFunction::Make<&OverlapPoint>(prefix + "OverlapPoint"),
            NativeFunction::Make<&ContactsOf>(prefix + "ContactsOf"),
            NativeFunction::Make<&IsSleeping>(prefix + "IsSleeping"),
            NativeFunction::Make<&Bounds>(prefix + "Bounds"),
            NativeFunction::Make<&Gravity>(prefix + "Gravity"),
            NativeFunction::Make<&SetGravity>(prefix + "SetGravity"),
            NativeFunction::Make<&SetDebugDraw>(prefix + "SetDebugDraw"),
            NativeFunction::Make<&CreateMesh>(prefix + "CreateCollisionMesh"),
            NativeFunction::Make<&ReleaseMesh>(prefix + "ReleaseCollisionMesh"),
        };
        extension.components = {
            MakeComponent<RigidBody>("Fenrir.Physics.RigidBody"),
            MakeComponent<Collider, Detail::ColliderLayout>("Fenrir.Physics.Collider"),
            MakeComponent<PhysicsMaterial>("Fenrir.Physics.PhysicsMaterial"),
            MakeComponent<CollisionFilter>("Fenrir.Physics.CollisionFilter"),
            MakeComponent<Trigger>("Fenrir.Physics.Trigger"),
            MakeComponent<ContactEvents>("Fenrir.Physics.ContactEvents"),
        };
        extension.events = {
            MakeEvent<ContactBegin, Detail::ContactBeginLayout>("Fenrir.Physics.ContactBegin"),
            MakeEvent<ContactEnd, Detail::ContactEndLayout>("Fenrir.Physics.ContactEnd"),
            MakeEvent<TriggerEnter, Detail::TriggerEnterLayout>("Fenrir.Physics.TriggerEnter"),
            MakeEvent<TriggerExit, Detail::TriggerExitLayout>("Fenrir.Physics.TriggerExit"),
        };
        return extension;
    }
} // namespace Fenrir
