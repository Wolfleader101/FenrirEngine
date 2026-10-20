#pragma once

// The physics backend seam (ADR-0010): a concrete class with no virtual calls and no backend type in sight. Its only
// implementation is in src/Backend/Jolt/, the only code that includes Jolt. Private and uninstalled.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Vehicles.hpp"

namespace Fenrir
{
    class CollisionLayers;
    class ILogger;
    class ThreadPool;
} // namespace Fenrir

namespace Fenrir::Backend
{
    /** @brief A body in one World. Valid until the body is destroyed. */
    struct BodyId
    {
        std::uint32_t value = ~0u;
    };

    /** @brief A joint's constraint in one World. Valid until the constraint is destroyed. */
    struct JointId
    {
        std::uint32_t value = ~0u;
    };

    /**
     * @brief Everything needed to make a joint's constraint. Each frame is relative to its body's origin, in the body's
     * axes, its position in world units (the body's scale already applied); the world end's frame is in world space.
     */
    struct JointDesc
    {
        BodyId bodyA;
        /** @brief The default BodyId: the world. */
        BodyId bodyB;
        JointFrame frameA;
        JointFrame frameB;
        /** @brief Its settings already in range: physics corrects them first. */
        JointKind kind;
        bool enabled = true;
        /** @brief Whether the two bodies collide with each other. While the joint is enabled and false, they do not;
         * nothing for a joint to the world. */
        bool collideConnected = false;
        /** @brief Where the constraint comes in the solve: lower first. Distinct for each joint, so the solve order
         * depends only on which joints exist, not on the order they were made and removed in. */
        std::uint32_t priority = 0;
    };

    /** @brief PROTOTYPE: a vehicle in one World. Valid until the vehicle is destroyed. */
    struct VehicleId
    {
        std::uint32_t value = ~0u;
    };

    /** @brief PROTOTYPE: one wheel, settings already in range. */
    struct VehicleWheelDesc
    {
        /** @brief Chassis-local from the body origin, the chassis's world scale applied: the centre at full
         * extension. */
        Math::Vec3 position = Math::Vec3(0.0f);
        /** @brief Its settings; vehicle, position and state ignored. */
        VehicleWheel wheel;
        /** @brief A rebuild's carried spin (rad/s), 0 for a new wheel. */
        float spin = 0.0f;
    };

    /** @brief PROTOTYPE: a differential by wheel index, -1 for none. */
    struct VehicleDifferentialDesc
    {
        int left = -1;
        int right = -1;
        VehicleDifferential settings;
    };

    /** @brief PROTOTYPE: an anti-roll bar by wheel index. */
    struct VehicleAntiRollBarDesc
    {
        int left = 0;
        int right = 1;
        float stiffness = 0.0f;
    };

    /** @brief PROTOTYPE: everything needed to make a vehicle on a made dynamic body. */
    struct VehicleDesc
    {
        BodyId body;
        /** @brief Settings already in range. */
        VehicleKind kind;
        std::vector<VehicleWheelDesc> wheels;
        /** @brief Empty: every wheel driven, each by its own differential (wheeled and motorcycle). */
        std::vector<VehicleDifferentialDesc> differentials;
        std::vector<VehicleAntiRollBarDesc> antiRollBars;
        VehicleProbe probe = VehicleProbe::Cylinder;
        float maxTilt = 0.0f;
        std::uint32_t priority = 0;
        /** @brief A rebuild's carried engine RPM (0: idle) and gear. */
        float rpm = 0.0f;
        int gear = 0;
    };

    /** @brief PROTOTYPE: one wheel as the last collision step left it. */
    struct VehicleWheelRead
    {
        bool onGround = false;
        std::uint32_t groundEntity = ~0u;
        std::uint32_t groundCollider = ~0u;
        Math::Vec3 contactPoint = Math::Vec3(0.0f);
        Math::Vec3 contactNormal = Math::Vec3(0.0f);
        Math::Vec3 groundVelocity = Math::Vec3(0.0f);
        float compression = 0.0f;
        float suspensionForce = 0.0f;
        float longitudinalSlip = 0.0f;
        float lateralSlip = 0.0f;
        float angularVelocity = 0.0f;
        /** @brief + right. */
        float steerAngle = 0.0f;
        /** @brief Radians, wrapped to [0, 2π). */
        float spinAngle = 0.0f;
    };

    /** @brief PROTOTYPE: a vehicle after a step. */
    struct VehicleRead
    {
        VehicleState state;
        std::vector<VehicleWheelRead> wheels;
    };

    /** @brief A character in one World. Valid until the character is destroyed. */
    struct CharacterId
    {
        std::uint32_t value = ~0u;
    };

    /**
     * @brief Everything needed to make a character: a collide-and-slide shape with a kinematic body of the same shape
     * that other bodies, queries and triggers see.
     */
    struct CharacterDesc
    {
        /** @brief Any shape but a mesh; it must pass CanMakeShape. */
        CollisionShape shape;
        /** @brief Its entity's world scale, baked into the shape. */
        Math::Vec3 scale = Math::Vec3(1.0f);
        /** @brief Its body's surface: what other bodies feel. A character's own moves ignore it. */
        PhysicsMaterial material;
        /** @brief Its body's filter, which also filters its moves. */
        CollisionFilter filter;
        /** @brief The shape's centre, as a collider's Transform is. */
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief The character's entity: it names it as body and collider in hits and contacts, and is its ID. */
        std::uint32_t entity = 0;
    };

    /**
     * @brief One tick's move of a character: its velocity, the carried velocity its last move left it, and the walking
     * and pushing rules of its CharacterBody.
     */
    struct CharacterMove
    {
        Math::Vec3 velocity = Math::Vec3(0.0f);
        Math::Vec3 carried = Math::Vec3(0.0f);
        float maxSlope = 0.0f;
        float stepHeight = 0.0f;
        float floorSnap = 0.0f;
        /** @brief Newtons: the most force a push of a dynamic body applies; 0 pushes nothing. */
        float pushForce = 0.0f;
        /** @brief Kilograms: the weight pressed on the ground, under the world's gravity; 0 presses nothing. */
        float mass = 0.0f;
        bool stopOnSlope = true;
    };

    /**
     * @brief Two bodies touching, as the backend sees it: a contact, or an overlap if one is a trigger. A pair stops
     * being found when both its bodies fall asleep (or one sleeps and the other is static); it keeps touching.
     */
    struct Touch
    {
        /** @brief The two bodies, in no meaningful order. */
        BodyId body1;
        BodyId body2;
        /** @brief Their entities, as given in their BodyDesc; kept after a body is destroyed. */
        std::uint32_t entity1 = 0;
        std::uint32_t entity2 = 0;
        /** @brief The entities of the colliders touching, one of each body (a compound body's part keeps its own). */
        std::uint32_t collider1 = 0;
        std::uint32_t collider2 = 0;
        bool isTrigger1 = false;
        bool isTrigger2 = false;
        /** @brief Whether each is a character's body (its inner body). */
        bool isCharacter1 = false;
        bool isCharacter2 = false;
        /** @brief Where they touch, in world space, as last found. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief Unit length, from body 1 to body 2, as last found. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How fast they were closing along `normal` when they began touching. */
        float relativeSpeed = 0.0f;
    };

    /** @brief A pair that began or stopped touching over one step. */
    struct TouchChange
    {
        enum class Kind : std::uint8_t
        {
            Begin,
            End
        };
        Kind kind;
        Touch touch;
    };

    /** @brief Where a move left a character, and what it touches there. */
    struct CharacterMoved
    {
        /** @brief The shape's centre. */
        Math::Vec3 position = Math::Vec3(0.0f);
        /** @brief The move's velocity, less the parts that went into what it touches at the end of the move. */
        Math::Vec3 velocity = Math::Vec3(0.0f);
        /** @brief The move's displacement over its time. */
        Math::Vec3 movedVelocity = Math::Vec3(0.0f);
        /**
         * @brief What the next move carries: none on walkable ground; the whole ground velocity this move rode if it
         * left that ground; otherwise the move's carried velocity, less the parts that went into what it touches.
         */
        Math::Vec3 carriedVelocity = Math::Vec3(0.0f);
        GroundState ground = GroundState::InAir;
        /** @brief On ground only: the ground body's entity and its collider's, ~0u otherwise. */
        std::uint32_t groundEntity = ~0u;
        std::uint32_t groundCollider = ~0u;
        Math::Vec3 groundPoint = Math::Vec3(0.0f);
        Math::Vec3 groundNormal = Math::Vec3(0.0f);
        /** @brief On ground only: the ground's velocity at the character, as this tick's step will move it. */
        Math::Vec3 groundVelocity = Math::Vec3(0.0f);
        Math::Vec3 groundAngularVelocity = Math::Vec3(0.0f);
        bool onWall = false;
        bool onCeiling = false;
        /**
         * @brief What it touches at the end of the move, triggers aside: one Touch per collider touched, the character
         * as body 1 (`normal` from it to what it touches), in the backend's order. `relativeSpeed` is the closing speed
         * along `normal` at the start of the move.
         */
        std::vector<Touch> contacts;
    };

    /** @brief How a body moves and responds: the parts of RigidBody the backend needs. */
    struct Motion
    {
        MotionType motionType = MotionType::Static;
        float mass = 1.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;
        float gravityScale = 1.0f;
    };

    /**
     * @brief One collider of a body: its shape, its entity's world scale (baked into the shape), and where it sits in
     * the body (relative to the body's pose, in world units).
     */
    struct ColliderPart
    {
        CollisionShape shape;
        Math::Vec3 scale = Math::Vec3(1.0f);
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief The collider's entity, kept with its part of the body's shape. */
        std::uint32_t entity = 0;
    };

    /** @brief Everything needed to make a body. */
    struct BodyDesc
    {
        Motion motion;
        /** @brief False for a collider with no RigidBody: it can never become kinematic or dynamic. */
        bool canMove = false;
        /**
         * @brief At least one. A single part on the body's own entity, at the body's pose, is the body's shape itself;
         * otherwise the parts are merged into one compound shape, each keeping its collider's entity.
         */
        std::vector<ColliderPart> parts;
        PhysicsMaterial material;
        CollisionFilter filter;
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Vec3 linearVelocity = Math::Vec3(0.0f);
        Math::Vec3 angularVelocity = Math::Vec3(0.0f);
        /** @brief The body's entity, kept with the body. */
        std::uint32_t entity = 0;
        /** @brief A trigger: a kinematic body, kept awake, that pushes nothing and finds static bodies too. */
        bool isTrigger = false;
    };

    /** @brief A body's pose and velocities after a step. */
    struct BodyState
    {
        Math::Vec3 position;
        Math::Quat rotation;
        Math::Vec3 linearVelocity;
        Math::Vec3 angularVelocity;
    };

    /** @brief What a joint applied to its bodyB over a step's last collision step, in world space. */
    struct JointLoad
    {
        /** @brief Newtons: every part of the joint holding, limiting or driving bodyB's position. */
        Math::Vec3 force = Math::Vec3(0.0f);
        /** @brief Newton metres, about the joint's anchor: every part holding, limiting or driving its rotation. */
        Math::Vec3 torque = Math::Vec3(0.0f);
    };

    /** @brief A non-owning reference to a callable, valid while the callable is: a callback with no virtual call and
     * no allocation. */
    template <typename Signature>
    class FunctionRef;

    template <typename Result, typename... Args>
    class FunctionRef<Result(Args...)>
    {
      public:
        // Never for another FunctionRef: copying one (a non-const lvalue, which this template would match better than
        // the copy constructor) would refer to that FunctionRef, not to its callable, and dangle once it is gone.
        template <typename Callable>
            requires(!std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef>)
        FunctionRef(Callable& callable) // NOLINT(google-explicit-constructor): passed straight to a call
            : m_callable(&callable), m_call([](void* target, Args... args) -> Result {
                  return (*static_cast<Callable*>(target))(std::forward<Args>(args)...);
              })
        {
        }

        Result operator()(Args... args) const
        {
            return m_call(m_callable, std::forward<Args>(args)...);
        }

      private:
        void* m_callable;
        Result (*m_call)(void*, Args...);
    };

    /** @brief Which bodies a query finds: those in a layer of `mask` whose own mask includes one of `layers` (any
     * mask when `layers` is 0), other than `ignoreEntity`'s, and triggers only if `hitTriggers`. */
    struct QueryFilter
    {
        std::uint32_t mask = ~0u;
        std::uint32_t layers = 0u;
        /** @brief The entity whose body is not found; the default matches no entity. */
        std::uint32_t ignoreEntity = ~0u;
        bool hitTriggers = false;
    };

    /** @brief Where a ray or a cast shape hit a body. */
    struct QueryHit
    {
        /** @brief The body's entity. */
        std::uint32_t entity;
        /** @brief The entity of the body's collider that was hit. */
        std::uint32_t collider;
        Math::Vec3 point;
        Math::Vec3 normal;
        float distance;
    };

    /** @brief Given each hit nearer than the distance it last returned (at first the query's maximum), in no
     * particular order; returns the farthest distance still wanted. */
    using HitSink = FunctionRef<float(const QueryHit&)>;

    /** @brief Given the entities of each body found and of its collider, once per collider; returns false to stop the
     * query. */
    using BodySink = FunctionRef<bool(std::uint32_t, std::uint32_t)>;

    /** @brief Where a shape overlaps one collider of a body. */
    struct Penetration
    {
        /** @brief The body's entity. */
        std::uint32_t entity;
        /** @brief The entity of the body's collider overlapped. */
        std::uint32_t collider;
        /** @brief The shape's deepest point inside the collider. */
        Math::Vec3 point;
        /** @brief The unit direction that moves the shape out along the shortest path. */
        Math::Vec3 normal;
        /** @brief How far along `normal` the shape must move: more than 0. */
        float depth;
    };

    /** @brief Given each penetration, in no particular order; a collider may be given more than once. */
    using PenetrationSink = FunctionRef<void(const Penetration&)>;

    /** @brief Given each edge of a shape, from one end to the other. */
    using EdgeSink = FunctionRef<void(const Math::Vec3&, const Math::Vec3&)>;

    /** @brief A world's settings when it is made. Gravity can change later; the capacities cannot. */
    struct WorldSettings
    {
        Math::Vec3 gravity;
        std::uint32_t maxBodies;
        std::uint32_t maxBodyPairs;
        std::uint32_t maxContacts;
        /**
         * @brief The threads a step spreads its work over, besides the stepping thread, which also works while it
         * waits. Null: a step runs on the stepping thread alone. Must outlive the world.
         */
        ThreadPool* workers = nullptr;
        /** @brief The layers' names, for logs; null logs layers by number. Must outlive the world. */
        const CollisionLayers* layerNames = nullptr;
    };

    /**
     * @brief The backend's process-wide state (allocator, type registration, log routing into `logger`), counted: the
     * first Context sets it up, the last one tears it down. Every World must end before the last Context does.
     */
    class Context
    {
      public:
        explicit Context(ILogger& logger);
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

      private:
        ILogger* m_logger;
    };

    /**
     * @brief The collision meshes made by CreateCollisionMesh and not yet released: their source data and their shapes,
     * each cooked the first time it is used. Shared by every World; must outlive them all.
     */
    class CollisionMeshes
    {
      public:
        explicit CollisionMeshes(ILogger& logger);
        ~CollisionMeshes();
        CollisionMeshes(const CollisionMeshes&) = delete;
        CollisionMeshes& operator=(const CollisionMeshes&) = delete;

        /** @brief Validates and keeps the data; the default handle (and an error) if it is invalid. Ids never repeat.
         */
        [[nodiscard]] CollisionMesh Create(std::span<const Math::Vec3> vertices,
                                           std::span<const std::uint32_t> indices);

        /** @brief Forgets a mesh; bodies already using its shapes keep them until they are reshaped or destroyed. */
        bool Release(CollisionMesh mesh);

        /** @brief The meshes' state, defined by the backend. */
        struct Impl;

        /** @brief The meshes' state, for the backend's own code (the World that cooks and uses their shapes). */
        [[nodiscard]] Impl& Implementation()
        {
            return *m_impl;
        }

      private:
        std::unique_ptr<Impl> m_impl;
    };

    /**
     * @brief One simulation. Every call but a query is made from the thread that runs the App; only a step's own work
     * runs on the worker threads. Queries may come from any threads at once, never during another call.
     */
    class World
    {
      public:
        World(const WorldSettings& settings, CollisionMeshes& meshes, ILogger& logger);
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;

        /**
         * @brief Whether a body can have `shape`: false for a hull or mesh whose collision mesh was released or cannot
         * be cooked as that kind (cooking it the first time, which logs an error if it fails). Says nothing otherwise.
         */
        [[nodiscard]] bool CanMakeShape(const CollisionShape& shape);

        /**
         * @brief Makes and adds the bodies in one batch, writing their ids to `ids` (the same length as `bodies`).
         * Every shape must pass CanMakeShape. A body that is not made gets the default BodyId.
         */
        void CreateBodies(std::span<const BodyDesc> bodies, std::span<BodyId> ids);

        /** @brief Removes and destroys the bodies in one batch. Every joint's constraint naming one of them must be
         * destroyed first. */
        void DestroyBodies(std::span<const BodyId> ids);

        /** @brief Makes and adds a joint's constraint between two made bodies (or a body and the world), waking them.
         */
        [[nodiscard]] JointId CreateJoint(const JointDesc& desc);

        /** @brief Removes and destroys a constraint, waking its bodies. */
        void DestroyJoint(JointId id);

        /** @brief Applies a joint's live settings (its kind's settings, which must be of the kind it was made with and
         * already in range, whether it is enabled, and whether its bodies collide with each other), waking its bodies.
         * From then on a hard limit beats the joint's Velocity motor, however strong, at every step. */
        void UpdateJoint(JointId id, const JointKind& kind, bool enabled, bool collideConnected);

        /** @brief Disables a joint after a step, as a break does: it holds nothing and its bodies collide with each
         * other from the next step. Wakes nothing: a joint is checked for breaking only while its bodies are awake. */
        void DisableJoint(JointId id);

        /**
         * @brief Makes the characters in one batch, in order, writing their ids to `ids` (the same length as
         * `characters`), each with its contacts and ground found where it stands. A character that is not made (the
         * world is full) gets the default CharacterId. Never during a step.
         */
        void CreateCharacters(std::span<const CharacterDesc> characters, std::span<CharacterId> ids);

        /** @brief Destroys the characters, each with the body that stands for it. Never during a step. */
        void DestroyCharacters(std::span<const CharacterId> ids);

        /**
         * @brief Moves a character by `move` over `dt` seconds, sliding along what it hits, stepping up stairs and
         * sticking to the ground going down, filtered as its body is and passing through triggers. Before a step, so
         * the bodies it touches are where the last step left them; its body is placed where it ends, at rest. If its
         * last move (or teleport) left it on walkable ground, it rides it: the ground's velocity now (after this
         * tick's MoveKinematic) is added to the move, and its velocity is trimmed in the ground's frame.
         *
         * Dynamic bodies it hits are given an impulse towards its speed at the contact, capped by `pushForce` and
         * never downward, and its ground body, if dynamic, is pressed by `mass` under the world's gravity at the
         * ground point: both act in the next step. A dynamic body never pushes it; a kinematic one moving into it does,
         * at that body's speed.
         *
         * Another character's body is a kinematic body moving at that character's last move's displacement over its
         * time, so standing on a character rides it; it never pushes this one sideways, and one standing on this
         * character is not in its way.
         */
        CharacterMoved MoveCharacter(CharacterId id, const CharacterMove& move, float dt);

        /** @brief Puts a character at a pose at once, with no sweep, and finds its contacts and ground there. */
        void TeleportCharacter(CharacterId id, const Math::Vec3& position, const Math::Quat& rotation);

        /** @brief Changes the surface of a character's body. */
        void SetCharacterMaterial(CharacterId id, const PhysicsMaterial& material);

        /** @brief Changes which layers a character's body is in and collides with, and so what its moves meet, waking
         * the bodies near it. */
        void SetCharacterFilter(CharacterId id, const CollisionFilter& filter);

        /** @brief Turns a character's shape where it stands, with no sweep: its next move pushes it out of anything
         * it now overlaps. Up stays world +Y. */
        void TurnCharacter(CharacterId id, const Math::Quat& rotation);

        /**
         * @brief Gives a character `shape` (which must pass CanMakeShape and not be a mesh) at `scale`, if it fits: its
         * body changes with it, and the character moves along world +Y so that the lowest point of its shape stays
         * where it was (`keepLowest`), or the highest. It does not fit where it would overlap what the character
         * collides with, triggers aside, by more than a little; then nothing changes and it returns false. Never
         * during a step.
         */
        bool ReshapeCharacter(CharacterId id, const CollisionShape& shape, const Math::Vec3& scale, bool keepLowest);

        // PROTOTYPE vehicles: never during a step.

        /** @brief Makes a vehicle on a made dynamic body: run inside each Step as a step listener. */
        [[nodiscard]] VehicleId CreateVehicle(const VehicleDesc& desc);
        /** @brief Before its body is destroyed. */
        void DestroyVehicle(VehicleId id);
        /** @brief Live settings: same kind, wheel count and maxAngle as it was made with. Patches each wheel's
         * settings in place, waking the chassis. */
        void UpdateVehicle(VehicleId id, const VehicleDesc& desc);
        /** @brief Applies driver input, clamped; wakes the chassis if `wake`. */
        void SetVehicleInput(VehicleId id, const VehicleInput& input, bool wake);
        /** @brief The vehicle's state and its wheels' after the last step. */
        [[nodiscard]] VehicleRead ReadVehicle(VehicleId id) const;
        /** @brief A wheel's pose relative to its chassis origin, for a compression, steer (+ right) and spin. */
        [[nodiscard]] JointFrame WheelPose(VehicleId id, std::size_t wheel, float compression, float steer,
                                           float spin) const;

        /** @brief Moves a body to a pose at once, waking it. */
        void Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation);

        /** @brief Gives a kinematic body the velocity that brings it to the pose in `dt` seconds. */
        void MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt);

        /** @brief Sets a body's velocities, waking it. */
        void SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular);

        /**
         * @brief Replaces a body's shape with one made from `parts` (as BodyDesc::parts, for the body of `entity`),
         * keeping its `mass`; its inertia follows the new shape. Every part's shape must pass CanMakeShape. Its joints'
         * constraints keep their anchors where they are relative to the body's origin, however its centre of mass
         * moves.
         */
        void SetShape(BodyId id, std::span<const ColliderPart> parts, std::uint32_t entity, float mass);

        /** @brief Changes a body's surface. */
        void SetMaterial(BodyId id, const PhysicsMaterial& material);

        /** @brief Changes which layers a body is in and collides with, waking it and the bodies near it. */
        void SetFilter(BodyId id, const CollisionFilter& filter);

        /** @brief Changes how a body moves. The body must have been made with `canMove`. */
        void SetMotion(BodyId id, const Motion& motion);

        /** @brief Changes a dynamic body's velocity by `impulse` over its mass, waking it. Other bodies ignore it. */
        void AddImpulse(BodyId id, const Math::Vec3& impulse);

        /** @brief Pushes a dynamic body with `force` during the next step only, waking it. Other bodies ignore it. */
        void AddForce(BodyId id, const Math::Vec3& force);

        /** @brief Twists a dynamic body with `torque` during the next step only, waking it. Other bodies ignore it. */
        void AddTorque(BodyId id, const Math::Vec3& torque);

        /** @brief Changes a dynamic body's angular velocity by `impulse` over its inertia, waking it. Other bodies
         * ignore it. */
        void AddAngularImpulse(BodyId id, const Math::Vec3& impulse);

        /** @brief The world's gravity, in metres per second squared. */
        [[nodiscard]] Math::Vec3 Gravity() const;

        /** @brief Changes the world's gravity, waking every body. */
        void SetGravity(const Math::Vec3& gravity);

        /** @brief A body's pose and velocities as the last step (or a teleport or velocity change since) left them. */
        [[nodiscard]] BodyState GetState(BodyId id) const;

        /**
         * @brief What the joint applied to its bodyB over the last Step's last collision step: the solver's impulses
         * over that collision step's time, along the axes the bodies' poses after the step give. Zero for a disabled
         * joint, and before its first Step. Stale while its bodies sleep: they are not solved.
         */
        [[nodiscard]] JointLoad Load(JointId id) const;

        /** @brief Diagnostic for FenrirPhysics's tests (PhysicsWorld::DistinctShapeCount): how many distinct backend
         * shapes the world's bodies use. */
        [[nodiscard]] std::size_t DistinctShapeCount() const;

        // Queries only read: they may be made from several threads at once, never during any other call.

        /** @brief Casts a ray from `origin` along the unit `direction` for `maxDistance`. A ray that starts inside a
         * solid shape (a box, sphere, capsule or hull) hits it at distance 0, its normal against `direction`; a mesh's
         * triangles are hit only on their front faces. */
        void CastRay(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                     const QueryFilter& filter, HitSink sink) const;

        /** @brief Sweeps `shape` (`scale` baked in, as for a body) from a pose along the unit `direction` for
         * `maxDistance`. Each hit's point is on the cast shape. */
        void CastShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                       const Math::Quat& rotation, const Math::Vec3& direction, float maxDistance,
                       const QueryFilter& filter, HitSink sink) const;

        /** @brief Sweeps `shape` as CastShape does, reporting only the hits that block a move: those after the start
         * (not overlapping or touching at distance 0) and moving towards the body. Each hit's distance is where the
         * shape touches. */
        void CastMove(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                      const Math::Quat& rotation, const Math::Vec3& direction, float maxDistance,
                      const QueryFilter& filter, HitSink sink) const;

        /** @brief Finds the bodies `shape` (`scale` baked in, as for a body) at a pose overlaps. */
        void CollideShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                          const Math::Quat& rotation, const QueryFilter& filter, BodySink sink) const;

        /** @brief Finds how deep `shape` (`scale` baked in, as for a body) at a pose is in each body it overlaps. */
        void CollideDeep(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                         const Math::Quat& rotation, const QueryFilter& filter, PenetrationSink sink) const;

        /** @brief Finds the bodies containing `point`. */
        void CollidePoint(const Math::Vec3& point, const QueryFilter& filter, BodySink sink) const;

        /** @brief Whether a body is asleep: resting, and not simulated until something wakes it. */
        [[nodiscard]] bool IsSleeping(BodyId id) const;

        /** @brief The body's world-space bounding box. */
        [[nodiscard]] Math::AABB Bounds(BodyId id) const;

        /**
         * @brief Gives `sink` the edges of a hull or mesh shape as the backend made it from its collision mesh, in the
         * shape's own space before scale: each edge of a hull's faces once, and the three edges of each of a mesh's
         * triangles. Nothing for a primitive, or a released or uncookable mesh. Only reads.
         */
        void ShapeEdges(const CollisionShape& shape, EdgeSink sink) const;

        /**
         * @brief Advances the simulation by `dt` seconds in `collisionSteps` collision steps, and finds which pairs of
         * bodies began and stopped touching. A body destroyed since the last step ends its touches here.
         */
        void Step(float dt, int collisionSteps);

        /** @brief The pairs that began or stopped touching in the last Step, in no particular order. Valid until the
         * next Step. */
        [[nodiscard]] std::span<const TouchChange> TouchChanges() const;

        /** @brief Every pair touching after the last Step, in no particular order. Valid until the next Step. */
        [[nodiscard]] std::span<const Touch> Touches() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir::Backend
