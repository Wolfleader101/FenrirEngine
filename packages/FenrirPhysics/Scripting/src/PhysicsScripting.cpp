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
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "JointStateMapping.hpp"
#include "QueryMirrors.hpp"

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
                           IRIS_FIELD_AS(Layout, hull, "_convexHull"), IRIS_FIELD_AS(Layout, mesh, "_mesh"));
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

template <>
struct Iris::EnumMapping<Fenrir::JointMotorMode>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointMotorMode";
};

template <>
struct Iris::EnumMapping<Fenrir::JointType>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointType";
};

template <>
struct Iris::StructMapping<Fenrir::JointSpring>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointSpring";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::JointSpring, frequency, "Frequency"),
                                                      IRIS_FIELD_AS(Fenrir::JointSpring, damping, "Damping"));
};

template <>
struct Iris::StructMapping<Fenrir::JointLimits>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointLimits";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Fenrir::JointLimits, enabled, "Enabled"), IRIS_FIELD_AS(Fenrir::JointLimits, min, "Min"),
        IRIS_FIELD_AS(Fenrir::JointLimits, max, "Max"), IRIS_FIELD_AS(Fenrir::JointLimits, spring, "Spring"));
};

template <>
struct Iris::StructMapping<Fenrir::JointMotor>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointMotor";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Fenrir::JointMotor, mode, "Mode"),
        IRIS_FIELD_AS(Fenrir::JointMotor, targetVelocity, "TargetVelocity"),
        IRIS_FIELD_AS(Fenrir::JointMotor, targetPosition, "TargetPosition"),
        IRIS_FIELD_AS(Fenrir::JointMotor, maxForce, "MaxForce"), IRIS_FIELD_AS(Fenrir::JointMotor, spring, "Spring"));
};

template <>
struct Iris::StructMapping<Fenrir::SwingTwistMotor>
{
    using Motor = Fenrir::SwingTwistMotor;
    static constexpr std::string_view typeName = "Fenrir.Physics.SwingTwistMotor";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Motor, mode, "Mode"), IRIS_FIELD_AS(Motor, targetRotation, "TargetRotation"),
                           IRIS_FIELD_AS(Motor, targetAngularVelocity, "TargetAngularVelocity"),
                           IRIS_FIELD_AS(Motor, maxTorque, "MaxTorque"), IRIS_FIELD_AS(Motor, spring, "Spring"));
};

template <>
struct Iris::StructMapping<Fenrir::DistanceJoint>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.DistanceJoint";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::DistanceJoint, minDistance, "MinDistance"),
                                                      IRIS_FIELD_AS(Fenrir::DistanceJoint, maxDistance, "MaxDistance"),
                                                      IRIS_FIELD_AS(Fenrir::DistanceJoint, spring, "Spring"));
};

template <>
struct Iris::StructMapping<Fenrir::HingeJoint>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.HingeJoint";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::HingeJoint, limits, "Limits"),
                                                      IRIS_FIELD_AS(Fenrir::HingeJoint, motor, "Motor"),
                                                      IRIS_FIELD_AS(Fenrir::HingeJoint, friction, "Friction"));
};

template <>
struct Iris::StructMapping<Fenrir::SliderJoint>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.SliderJoint";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::SliderJoint, limits, "Limits"),
                                                      IRIS_FIELD_AS(Fenrir::SliderJoint, motor, "Motor"),
                                                      IRIS_FIELD_AS(Fenrir::SliderJoint, friction, "Friction"));
};

template <>
struct Iris::StructMapping<Fenrir::SwingTwistJoint>
{
    using Kind = Fenrir::SwingTwistJoint;
    static constexpr std::string_view typeName = "Fenrir.Physics.SwingTwistJoint";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Kind, swingYLimit, "SwingYLimit"), IRIS_FIELD_AS(Kind, swingZLimit, "SwingZLimit"),
        IRIS_FIELD_AS(Kind, twistMin, "TwistMin"), IRIS_FIELD_AS(Kind, twistMax, "TwistMax"),
        IRIS_FIELD_AS(Kind, friction, "Friction"), IRIS_FIELD_AS(Kind, motor, "Motor"));
};

namespace Fenrir::Detail
{
    // FixedJoint and BallJoint are empty, which Iris cannot map, so these stand in for them: the one byte an empty
    // struct takes, as a field the C# FixedJoint and BallJoint keep private.
    struct FixedJointLayout
    {
        std::uint8_t unused;
    };
    static_assert(sizeof(FixedJointLayout) == sizeof(FixedJoint) && alignof(FixedJointLayout) == alignof(FixedJoint),
                  "FixedJoint changed shape: update FixedJointLayout and the C# FixedJoint.");

    struct BallJointLayout
    {
        std::uint8_t unused;
    };
    static_assert(sizeof(BallJointLayout) == sizeof(BallJoint) && alignof(BallJointLayout) == alignof(BallJoint),
                  "BallJoint changed shape: update BallJointLayout and the C# BallJoint.");

    // JointKind keeps its tag and union private, and Joint holds Entities, so these stand in for them, as for
    // CollisionShape and the events: the same members in the same order. MakeComponent checks the size and alignment;
    // the scripting tests read and write real joints through them.
    struct JointKindLayout
    {
        JointType type = JointType::Fixed;
        union {
            FixedJointLayout fixed{};
            BallJointLayout ball;
            DistanceJoint distance;
            HingeJoint hinge;
            SliderJoint slider;
            SwingTwistJoint swingTwist;
        };
    };
    static_assert(sizeof(JointKindLayout) == sizeof(JointKind) && alignof(JointKindLayout) == alignof(JointKind),
                  "JointKind changed shape: update JointKindLayout and the C# JointKind.");

    struct JointLayout
    {
        EntityLinkView bodyA;
        EntityLinkView bodyB;
        JointFrame frameA;
        JointFrame frameB;
        JointKindLayout kind;
        bool enabled;
        bool collideConnected;
        float breakForce;
        float breakTorque;
    };
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::FixedJointLayout>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.FixedJoint";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::FixedJointLayout, unused, "_unused"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::BallJointLayout>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.BallJoint";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::BallJointLayout, unused, "_unused"));
};

// The tag and every kind, in declaration order, each kind at the union's offset (the C# explicit layout mirrors it).
template <>
struct Iris::StructMapping<Fenrir::Detail::JointKindLayout>
{
    using Layout = Fenrir::Detail::JointKindLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.JointKind";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, fixed, "_fixed"),
                           IRIS_FIELD_AS(Layout, ball, "_ball"), IRIS_FIELD_AS(Layout, distance, "_distance"),
                           IRIS_FIELD_AS(Layout, hinge, "_hinge"), IRIS_FIELD_AS(Layout, slider, "_slider"),
                           IRIS_FIELD_AS(Layout, swingTwist, "_swingTwist"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::JointLayout>
{
    using Layout = Fenrir::Detail::JointLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.Joint";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, bodyA, "BodyA"), IRIS_FIELD_AS(Layout, bodyB, "BodyB"),
        IRIS_FIELD_AS(Layout, frameA, "FrameA"), IRIS_FIELD_AS(Layout, frameB, "FrameB"),
        IRIS_FIELD_AS(Layout, kind, "Kind"), IRIS_FIELD_AS(Layout, enabled, "Enabled"),
        IRIS_FIELD_AS(Layout, collideConnected, "CollideConnected"), IRIS_FIELD_AS(Layout, breakForce, "BreakForce"),
        IRIS_FIELD_AS(Layout, breakTorque, "BreakTorque"));
};

template <>
struct Iris::EnumMapping<Fenrir::GroundState>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.GroundState";
};

namespace Fenrir::Detail
{
    // CharacterState holds Entities, so these stand in for it and CharacterBody, as for Joint: the same members in the
    // same order, each Entity as the link the engine stores. MakeComponent checks the size and alignment; the scripting
    // tests read and write real characters through them.
    struct CharacterStateLayout
    {
        GroundState ground;
        EntityLinkView groundBody;
        EntityLinkView groundCollider;
        Math::Vec3 groundPoint;
        Math::Vec3 groundNormal;
        Math::Vec3 groundVelocity;
        Math::Vec3 groundAngularVelocity;
        Math::Vec3 movedVelocity;
        Math::Vec3 carriedVelocity;
        bool onWall;
        bool onCeiling;
        bool shapeBlocked;
    };
    static_assert(sizeof(CharacterStateLayout) == sizeof(CharacterState) &&
                      alignof(CharacterStateLayout) == alignof(CharacterState),
                  "CharacterState changed shape: update CharacterStateLayout and the C# CharacterState.");

    struct CharacterBodyLayout
    {
        Math::Vec3 velocity;
        float maxSlope;
        float stepHeight;
        float floorSnap;
        float pushForce;
        float mass;
        bool stopOnSlope;
        bool interpolate;
        bool enabled;
        CharacterStateLayout state;
    };
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::CharacterStateLayout>
{
    using Layout = Fenrir::Detail::CharacterStateLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.CharacterState";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, ground, "Ground"), IRIS_FIELD_AS(Layout, groundBody, "GroundBody"),
        IRIS_FIELD_AS(Layout, groundCollider, "GroundCollider"), IRIS_FIELD_AS(Layout, groundPoint, "GroundPoint"),
        IRIS_FIELD_AS(Layout, groundNormal, "GroundNormal"), IRIS_FIELD_AS(Layout, groundVelocity, "GroundVelocity"),
        IRIS_FIELD_AS(Layout, groundAngularVelocity, "GroundAngularVelocity"),
        IRIS_FIELD_AS(Layout, movedVelocity, "MovedVelocity"),
        IRIS_FIELD_AS(Layout, carriedVelocity, "CarriedVelocity"), IRIS_FIELD_AS(Layout, onWall, "OnWall"),
        IRIS_FIELD_AS(Layout, onCeiling, "OnCeiling"), IRIS_FIELD_AS(Layout, shapeBlocked, "ShapeBlocked"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::CharacterBodyLayout>
{
    using Layout = Fenrir::Detail::CharacterBodyLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.CharacterBody";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, velocity, "Velocity"), IRIS_FIELD_AS(Layout, maxSlope, "MaxSlope"),
        IRIS_FIELD_AS(Layout, stepHeight, "StepHeight"), IRIS_FIELD_AS(Layout, floorSnap, "FloorSnap"),
        IRIS_FIELD_AS(Layout, pushForce, "PushForce"), IRIS_FIELD_AS(Layout, mass, "Mass"),
        IRIS_FIELD_AS(Layout, stopOnSlope, "StopOnSlope"), IRIS_FIELD_AS(Layout, interpolate, "Interpolate"),
        IRIS_FIELD_AS(Layout, enabled, "Enabled"), IRIS_FIELD_AS(Layout, state, "State"));
};

template <>
struct Iris::EnumMapping<Fenrir::VehicleProbe>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleProbe";
};

template <>
struct Iris::EnumMapping<Fenrir::TransmissionMode>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.TransmissionMode";
};

template <>
struct Iris::EnumMapping<Fenrir::VehicleType>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleType";
};

template <>
struct Iris::EnumMapping<Fenrir::VehicleTrackSide>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleTrackSide";
};

template <>
struct Iris::StructMapping<Fenrir::VehicleInput>
{
    using Input = Fenrir::VehicleInput;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleInput";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Input, forward, "Forward"), IRIS_FIELD_AS(Input, steer, "Steer"),
                           IRIS_FIELD_AS(Input, brake, "Brake"), IRIS_FIELD_AS(Input, handbrake, "Handbrake"),
                           IRIS_FIELD_AS(Input, leftTrack, "LeftTrack"), IRIS_FIELD_AS(Input, rightTrack, "RightTrack"),
                           IRIS_FIELD_AS(Input, gear, "Gear"), IRIS_FIELD_AS(Input, clutch, "Clutch"));
};

template <>
struct Iris::StructMapping<Fenrir::VehicleSuspension>
{
    using Suspension = Fenrir::VehicleSuspension;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleSuspension";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Suspension, travel, "Travel"), IRIS_FIELD_AS(Suspension, spring, "Spring"),
                           IRIS_FIELD_AS(Suspension, preload, "Preload"));
};

template <>
struct Iris::StructMapping<Fenrir::VehicleTyre>
{
    using Tyre = Fenrir::VehicleTyre;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleTyre";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Tyre, peakSlip, "PeakSlip"), IRIS_FIELD_AS(Tyre, peakFriction, "PeakFriction"),
        IRIS_FIELD_AS(Tyre, slideSlip, "SlideSlip"), IRIS_FIELD_AS(Tyre, slideFriction, "SlideFriction"));
};

template <>
struct Iris::StructMapping<Fenrir::VehicleEngine>
{
    using Engine = Fenrir::VehicleEngine;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleEngine";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Engine, maxTorque, "MaxTorque"), IRIS_FIELD_AS(Engine, minRpm, "MinRpm"),
        IRIS_FIELD_AS(Engine, maxRpm, "MaxRpm"), IRIS_FIELD_AS(Engine, peakTorqueRpm, "PeakTorqueRpm"),
        IRIS_FIELD_AS(Engine, torqueAtMinRpm, "TorqueAtMinRpm"),
        IRIS_FIELD_AS(Engine, torqueAtMaxRpm, "TorqueAtMaxRpm"), IRIS_FIELD_AS(Engine, inertia, "Inertia"),
        IRIS_FIELD_AS(Engine, angularDamping, "AngularDamping"));
};

template <>
struct Iris::StructMapping<Fenrir::VehicleTransmission>
{
    using Transmission = Fenrir::VehicleTransmission;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleTransmission";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Transmission, mode, "Mode"), IRIS_FIELD_AS(Transmission, gearCount, "GearCount"),
        IRIS_FIELD_AS(Transmission, firstGear, "FirstGear"), IRIS_FIELD_AS(Transmission, topGear, "TopGear"),
        IRIS_FIELD_AS(Transmission, reverseGear, "ReverseGear"), IRIS_FIELD_AS(Transmission, shiftUpRpm, "ShiftUpRpm"),
        IRIS_FIELD_AS(Transmission, shiftDownRpm, "ShiftDownRpm"),
        IRIS_FIELD_AS(Transmission, switchTime, "SwitchTime"),
        IRIS_FIELD_AS(Transmission, clutchReleaseTime, "ClutchReleaseTime"),
        IRIS_FIELD_AS(Transmission, switchLatency, "SwitchLatency"),
        IRIS_FIELD_AS(Transmission, clutchStrength, "ClutchStrength"));
};

template <>
struct Iris::StructMapping<Fenrir::WheeledVehicle>
{
    using Kind = Fenrir::WheeledVehicle;
    static constexpr std::string_view typeName = "Fenrir.Physics.WheeledVehicle";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Kind, engine, "Engine"), IRIS_FIELD_AS(Kind, transmission, "Transmission"),
                           IRIS_FIELD_AS(Kind, limitedSlipRatio, "LimitedSlipRatio"));
};

template <>
struct Iris::StructMapping<Fenrir::MotorcycleLean>
{
    using Lean = Fenrir::MotorcycleLean;
    static constexpr std::string_view typeName = "Fenrir.Physics.MotorcycleLean";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Lean, enabled, "Enabled"), IRIS_FIELD_AS(Lean, maxAngle, "MaxAngle"),
                           IRIS_FIELD_AS(Lean, limitSteering, "LimitSteering"));
};

template <>
struct Iris::StructMapping<Fenrir::MotorcycleVehicle>
{
    using Kind = Fenrir::MotorcycleVehicle;
    static constexpr std::string_view typeName = "Fenrir.Physics.MotorcycleVehicle";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Kind, engine, "Engine"), IRIS_FIELD_AS(Kind, transmission, "Transmission"),
        IRIS_FIELD_AS(Kind, limitedSlipRatio, "LimitedSlipRatio"), IRIS_FIELD_AS(Kind, lean, "Lean"));
};

template <>
struct Iris::StructMapping<Fenrir::VehicleTrack>
{
    using Track = Fenrir::VehicleTrack;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleTrack";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Track, inertia, "Inertia"), IRIS_FIELD_AS(Track, angularDamping, "AngularDamping"),
        IRIS_FIELD_AS(Track, maxBrakeTorque, "MaxBrakeTorque"),
        IRIS_FIELD_AS(Track, differentialRatio, "DifferentialRatio"),
        IRIS_FIELD_AS(Track, longitudinalFriction, "LongitudinalFriction"),
        IRIS_FIELD_AS(Track, lateralFriction, "LateralFriction"));
};

template <>
struct Iris::StructMapping<Fenrir::TrackedVehicle>
{
    using Kind = Fenrir::TrackedVehicle;
    static constexpr std::string_view typeName = "Fenrir.Physics.TrackedVehicle";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Kind, engine, "Engine"), IRIS_FIELD_AS(Kind, transmission, "Transmission"),
                           IRIS_FIELD_AS(Kind, left, "Left"), IRIS_FIELD_AS(Kind, right, "Right"));
};

template <>
struct Iris::StructMapping<Fenrir::VehicleState>
{
    using State = Fenrir::VehicleState;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleState";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(State, forwardSpeed, "ForwardSpeed"), IRIS_FIELD_AS(State, engineRpm, "EngineRpm"),
        IRIS_FIELD_AS(State, gear, "Gear"), IRIS_FIELD_AS(State, clutch, "Clutch"),
        IRIS_FIELD_AS(State, shifting, "Shifting"), IRIS_FIELD_AS(State, wheelsOnGround, "WheelsOnGround"),
        IRIS_FIELD_AS(State, leanAngle, "LeanAngle"), IRIS_FIELD_AS(State, leftTrackSpeed, "LeftTrackSpeed"),
        IRIS_FIELD_AS(State, rightTrackSpeed, "RightTrackSpeed"));
};

namespace Fenrir::Detail
{
    // TrailerVehicle is empty, which Iris cannot map, so this stands in for it, as for FixedJoint.
    struct TrailerVehicleLayout
    {
        std::uint8_t unused;
    };
    static_assert(sizeof(TrailerVehicleLayout) == sizeof(TrailerVehicle) &&
                      alignof(TrailerVehicleLayout) == alignof(TrailerVehicle),
                  "TrailerVehicle changed shape: update TrailerVehicleLayout and the C# TrailerVehicle.");

    // VehicleKind keeps its tag and union private, and the wheel components hold Entities, so these stand in for them,
    // as for JointKind and Joint: the same members in the same order, each Entity as the link the engine stores.
    // MakeComponent checks the size and alignment; the scripting tests read and write real vehicles through them.
    struct VehicleKindLayout
    {
        VehicleType type = VehicleType::Wheeled;
        union {
            WheeledVehicle wheeled{};
            MotorcycleVehicle motorcycle;
            TrackedVehicle tracked;
            TrailerVehicleLayout trailer;
        };
    };
    static_assert(sizeof(VehicleKindLayout) == sizeof(VehicleKind) &&
                      alignof(VehicleKindLayout) == alignof(VehicleKind),
                  "VehicleKind changed shape: update VehicleKindLayout and the C# VehicleKind.");

    struct VehicleLayout
    {
        VehicleKindLayout kind;
        VehicleInput input;
        VehicleProbe probe;
        float maxTilt;
        bool enabled;
        VehicleState state;
    };

    struct VehicleWheelStateLayout
    {
        bool onGround;
        EntityLinkView groundBody;
        EntityLinkView groundCollider;
        Math::Vec3 contactPoint;
        Math::Vec3 contactNormal;
        Math::Vec3 groundVelocity;
        float compression;
        float suspensionForce;
        float longitudinalSlip;
        float lateralSlip;
        float angularVelocity;
        float steerAngle;
    };
    static_assert(sizeof(VehicleWheelStateLayout) == sizeof(VehicleWheelState) &&
                      alignof(VehicleWheelStateLayout) == alignof(VehicleWheelState),
                  "VehicleWheelState changed shape: update VehicleWheelStateLayout and the C# VehicleWheelState.");

    struct VehicleWheelLayout
    {
        EntityLinkView vehicle;
        Math::Vec3 position;
        VehicleTrackSide track;
        float radius;
        float width;
        float inertia;
        float angularDamping;
        float caster;
        float camber;
        float toe;
        VehicleSuspension suspension;
        float maxSteerAngle;
        float maxBrakeTorque;
        float maxHandbrakeTorque;
        VehicleTyre longitudinal;
        VehicleTyre lateral;
        VehicleWheelStateLayout state;
    };

    struct VehicleDifferentialLayout
    {
        EntityLinkView leftWheel;
        EntityLinkView rightWheel;
        float ratio;
        float leftRightSplit;
        float limitedSlipRatio;
        float torqueShare;
    };

    struct VehicleAntiRollBarLayout
    {
        EntityLinkView leftWheel;
        EntityLinkView rightWheel;
        float stiffness;
    };
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::TrailerVehicleLayout>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.TrailerVehicle";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::TrailerVehicleLayout, unused, "_unused"));
};

// The tag and every kind, in declaration order, each kind at the union's offset (the C# explicit layout mirrors it).
template <>
struct Iris::StructMapping<Fenrir::Detail::VehicleKindLayout>
{
    using Layout = Fenrir::Detail::VehicleKindLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleKind";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, wheeled, "_wheeled"),
                           IRIS_FIELD_AS(Layout, motorcycle, "_motorcycle"), IRIS_FIELD_AS(Layout, tracked, "_tracked"),
                           IRIS_FIELD_AS(Layout, trailer, "_trailer"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::VehicleLayout>
{
    using Layout = Fenrir::Detail::VehicleLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.Vehicle";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, kind, "Kind"), IRIS_FIELD_AS(Layout, input, "Input"),
                           IRIS_FIELD_AS(Layout, probe, "Probe"), IRIS_FIELD_AS(Layout, maxTilt, "MaxTilt"),
                           IRIS_FIELD_AS(Layout, enabled, "Enabled"), IRIS_FIELD_AS(Layout, state, "State"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::VehicleWheelStateLayout>
{
    using Layout = Fenrir::Detail::VehicleWheelStateLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleWheelState";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, onGround, "OnGround"), IRIS_FIELD_AS(Layout, groundBody, "GroundBody"),
        IRIS_FIELD_AS(Layout, groundCollider, "GroundCollider"), IRIS_FIELD_AS(Layout, contactPoint, "ContactPoint"),
        IRIS_FIELD_AS(Layout, contactNormal, "ContactNormal"), IRIS_FIELD_AS(Layout, groundVelocity, "GroundVelocity"),
        IRIS_FIELD_AS(Layout, compression, "Compression"), IRIS_FIELD_AS(Layout, suspensionForce, "SuspensionForce"),
        IRIS_FIELD_AS(Layout, longitudinalSlip, "LongitudinalSlip"), IRIS_FIELD_AS(Layout, lateralSlip, "LateralSlip"),
        IRIS_FIELD_AS(Layout, angularVelocity, "AngularVelocity"), IRIS_FIELD_AS(Layout, steerAngle, "SteerAngle"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::VehicleWheelLayout>
{
    using Layout = Fenrir::Detail::VehicleWheelLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleWheel";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, vehicle, "Vehicle"), IRIS_FIELD_AS(Layout, position, "Position"),
        IRIS_FIELD_AS(Layout, track, "Track"), IRIS_FIELD_AS(Layout, radius, "Radius"),
        IRIS_FIELD_AS(Layout, width, "Width"), IRIS_FIELD_AS(Layout, inertia, "Inertia"),
        IRIS_FIELD_AS(Layout, angularDamping, "AngularDamping"), IRIS_FIELD_AS(Layout, caster, "Caster"),
        IRIS_FIELD_AS(Layout, camber, "Camber"), IRIS_FIELD_AS(Layout, toe, "Toe"),
        IRIS_FIELD_AS(Layout, suspension, "Suspension"), IRIS_FIELD_AS(Layout, maxSteerAngle, "MaxSteerAngle"),
        IRIS_FIELD_AS(Layout, maxBrakeTorque, "MaxBrakeTorque"),
        IRIS_FIELD_AS(Layout, maxHandbrakeTorque, "MaxHandbrakeTorque"),
        IRIS_FIELD_AS(Layout, longitudinal, "Longitudinal"), IRIS_FIELD_AS(Layout, lateral, "Lateral"),
        IRIS_FIELD_AS(Layout, state, "State"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::VehicleDifferentialLayout>
{
    using Layout = Fenrir::Detail::VehicleDifferentialLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleDifferential";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Layout, leftWheel, "LeftWheel"), IRIS_FIELD_AS(Layout, rightWheel, "RightWheel"),
        IRIS_FIELD_AS(Layout, ratio, "Ratio"), IRIS_FIELD_AS(Layout, leftRightSplit, "LeftRightSplit"),
        IRIS_FIELD_AS(Layout, limitedSlipRatio, "LimitedSlipRatio"), IRIS_FIELD_AS(Layout, torqueShare, "TorqueShare"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::VehicleAntiRollBarLayout>
{
    using Layout = Fenrir::Detail::VehicleAntiRollBarLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.VehicleAntiRollBar";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Layout, leftWheel, "LeftWheel"),
                                                      IRIS_FIELD_AS(Layout, rightWheel, "RightWheel"),
                                                      IRIS_FIELD_AS(Layout, stiffness, "Stiffness"));
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

    struct JointBrokenLayout
    {
        EntityLinkView joint;
        EntityLinkView bodyA;
        EntityLinkView bodyB;
        Math::Vec3 force;
        Math::Vec3 torque;
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

template <>
struct Iris::StructMapping<Fenrir::Detail::JointBrokenLayout>
{
    using Layout = Fenrir::Detail::JointBrokenLayout;
    static constexpr std::string_view typeName = "Fenrir.Physics.JointBroken";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, joint, "Joint"), IRIS_FIELD_AS(Layout, bodyA, "BodyA"),
                           IRIS_FIELD_AS(Layout, bodyB, "BodyB"), IRIS_FIELD_AS(Layout, force, "Force"),
                           IRIS_FIELD_AS(Layout, torque, "Torque"));
};

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

        using Detail::ScriptMoveCastHit;
        using Detail::ScriptPenetrationHit;
        using Detail::ScriptQueryFilter;

        // The C# mirrors of the other query types (Managed/Fenrir.Physics/Queries.cs), with entities as their 32-bit
        // values, since a Fenrir::Entity holds a pointer to its list. They cross only by pointer, so nothing checks
        // them at load: the sizes are checked here and the scripting tests read every field back. QueryMirrors.hpp has
        // the rest, with mappings the tests check.

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
                               .layers = filter->layers,
                               .ignoreBody = filter->ignoreBody.value == Entity::Null
                                                 ? Entity()
                                                 : context.Entities().HandleOf(filter->ignoreBody.value),
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

        bool Raycast(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                     const Math::Vec3* direction, float maxDistance, const ScriptQueryFilter* filter, ScriptHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = ScriptHit{};
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
            *hit = ToScript(*found);
            return true;
        }

        std::int32_t RaycastAll(const CallContext& context, PhysicsScripting& physics, const Math::Vec3* origin,
                                const Math::Vec3* direction, float maxDistance, const ScriptQueryFilter* filter,
                                ScriptHit* hits, std::int32_t capacity)
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

        bool ShapeCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                       const Transform* pose, const Math::Vec3* direction, float maxDistance,
                       const ScriptQueryFilter* filter, ScriptHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = ScriptHit{};
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
            *hit = ToScript(*found);
            return true;
        }

        std::int32_t ShapeCastAll(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                                  const Transform* pose, const Math::Vec3* direction, float maxDistance,
                                  const ScriptQueryFilter* filter, ScriptHit* hits, std::int32_t capacity)
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
                           ToScript<ShapeCastHit>);
            return static_cast<std::int32_t>(count);
        }

        bool MoveCast(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                      const Transform* pose, const Math::Vec3* motion, const ScriptQueryFilter* filter,
                      ScriptMoveCastHit* hit)
        {
            if (hit == nullptr)
            {
                return false;
            }
            *hit = ScriptMoveCastHit{};
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
            *hit = ScriptMoveCastHit{.body = {found->body.GetId()},
                                     .collider = {found->collider.GetId()},
                                     .point = found->point,
                                     .normal = found->normal,
                                     .distance = found->distance};
            return true;
        }

        std::int32_t Overlap(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                             const Transform* pose, const ScriptQueryFilter* filter, ScriptOverlapHit* hits,
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
                                  const Transform* pose, const ScriptQueryFilter* filter, ScriptPenetrationHit* hits,
                                  std::int32_t capacity)
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
                               return ScriptPenetrationHit{.body = {hit.body.GetId()},
                                                           .collider = {hit.collider.GetId()},
                                                           .point = hit.point,
                                                           .normal = hit.normal,
                                                           .depth = hit.depth};
                           });
            return static_cast<std::int32_t>(count);
        }

        bool Depenetrate(const CallContext& context, PhysicsScripting& physics, const CollisionShape* shape,
                         const Transform* pose, const ScriptQueryFilter* filter, Math::Vec3* offset)
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
                                  const ScriptQueryFilter* filter, ScriptOverlapHit* hits, std::int32_t capacity)
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

        std::int32_t ContactsOf(const CallContext& context, PhysicsScripting& physics, std::uint32_t body,
                                ScriptContact* contacts, std::int32_t capacity)
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

        bool IsSleeping(const CallContext& context, PhysicsScripting& physics, std::uint32_t body)
        {
            PhysicsWorld* world = physics.WorldOf(context, "IsSleeping");
            if (world == nullptr || !physics.HasBody(context, body, "IsSleeping"))
            {
                return false;
            }
            return world->IsSleeping(context.Entities().HandleOf(body));
        }

        bool Bounds(const CallContext& context, PhysicsScripting& physics, std::uint32_t body, ScriptAabb* bounds)
        {
            if (bounds == nullptr)
            {
                return false;
            }
            *bounds = ScriptAabb{};
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
            *bounds = ScriptAabb{.min = found->min, .max = found->max};
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
        extension.components = {
            MakeComponent<RigidBody>("Fenrir.Physics.RigidBody"),
            MakeComponent<Collider, Detail::ColliderLayout>("Fenrir.Physics.Collider"),
            MakeComponent<PhysicsMaterial>("Fenrir.Physics.PhysicsMaterial"),
            MakeComponent<CollisionFilter>("Fenrir.Physics.CollisionFilter"),
            MakeComponent<Trigger>("Fenrir.Physics.Trigger"),
            MakeComponent<ContactEvents>("Fenrir.Physics.ContactEvents"),
            MakeComponent<Joint, Detail::JointLayout>("Fenrir.Physics.Joint"),
            MakeComponent<CharacterBody, Detail::CharacterBodyLayout>("Fenrir.Physics.CharacterBody"),
            MakeComponent<Vehicle, Detail::VehicleLayout>("Fenrir.Physics.Vehicle"),
            MakeComponent<VehicleWheel, Detail::VehicleWheelLayout>("Fenrir.Physics.VehicleWheel"),
            MakeComponent<VehicleDifferential, Detail::VehicleDifferentialLayout>("Fenrir.Physics.VehicleDifferential"),
            MakeComponent<VehicleAntiRollBar, Detail::VehicleAntiRollBarLayout>("Fenrir.Physics.VehicleAntiRollBar"),
        };
        extension.events = {
            MakeEvent<ContactBegin, Detail::ContactBeginLayout>("Fenrir.Physics.ContactBegin"),
            MakeEvent<ContactEnd, Detail::ContactEndLayout>("Fenrir.Physics.ContactEnd"),
            MakeEvent<TriggerEnter, Detail::TriggerEnterLayout>("Fenrir.Physics.TriggerEnter"),
            MakeEvent<TriggerExit, Detail::TriggerExitLayout>("Fenrir.Physics.TriggerExit"),
            MakeEvent<JointBroken, Detail::JointBrokenLayout>("Fenrir.Physics.JointBroken"),
        };
        return extension;
    }
} // namespace Fenrir
