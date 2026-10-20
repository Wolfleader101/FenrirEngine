#pragma once

// PROTOTYPE (prototype/fenrir-physics-vehicles-spike, never merged): the vehicle model proposed by "How is a vehicle
// modelled in the ECS?" (ADR-0015), "Which settings does each vehicle kind expose?" and "How does a vehicle step each
// tick, and what does game code read back?", made to be driven in the DemoApp. Only as much doc as the spike needs.

#include <cassert>
#include <cstdint>
#include <numbers>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Joints.hpp"

namespace Fenrir
{
    /** @brief Normalised driver input: game code writes it in Tick, physics applies it in PostTick and never writes it.
     */
    struct VehicleInput
    {
        /** @brief [-1, 1]: negative reverses (Automatic). */
        float forward = 0.0f;
        /** @brief [-1, 1], + is right. Tracked vehicles ignore it. */
        float steer = 0.0f;
        /** @brief [0, 1]. */
        float brake = 0.0f;
        /** @brief [0, 1]. Tracked vehicles ignore it. */
        float handbrake = 0.0f;
        /** @brief Tracked only: each track's speed ratio, [-1, 1]; 0 holds the track still. */
        float leftTrack = 1.0f;
        float rightTrack = 1.0f;
        /** @brief Manual only: 0 neutral, 1..n forward, -1 reverse; clamped. */
        int gear = 0;
        /** @brief Manual only: the clutch pedal, [0, 1], 0 engaged. */
        float clutch = 0.0f;
    };

    struct VehicleEngine
    {
        float maxTorque = 500.0f;
        float minRPM = 1000.0f;
        float maxRPM = 6000.0f;
        float peakTorqueRPM = 4000.0f;
        /** @brief Fractions of maxTorque at minRPM and maxRPM. */
        float torqueAtMinRPM = 0.8f;
        float torqueAtMaxRPM = 0.8f;
        float inertia = 0.5f;
        float angularDamping = 0.2f;
    };

    enum class TransmissionMode : std::uint8_t
    {
        Automatic,
        Manual
    };

    struct VehicleTransmission
    {
        TransmissionMode mode = TransmissionMode::Automatic;
        /** @brief Forward gears, spaced geometrically from firstGear to topGear. */
        int gearCount = 5;
        float firstGear = 2.66f;
        float topGear = 0.74f;
        /** @brief A positive magnitude. */
        float reverseGear = 2.90f;
        float shiftUpRPM = 4000.0f;
        float shiftDownRPM = 2000.0f;
        float switchTime = 0.5f;
        float clutchReleaseTime = 0.3f;
        float switchLatency = 0.5f;
        float clutchStrength = 10.0f;
    };

    struct VehicleTrack
    {
        float inertia = 10.0f;
        float angularDamping = 0.5f;
        float maxBrakeTorque = 15000.0f;
        float differentialRatio = 6.0f;
        float longitudinalFriction = 4.0f;
        float lateralFriction = 2.0f;
    };

    struct MotorcycleLean
    {
        /** @brief Disabled, the bike falls over. */
        bool enabled = true;
        /** @brief Radians. Changing it rebuilds the vehicle privately. */
        float maxAngle = std::numbers::pi_v<float> / 4.0f;
        /** @brief Caps steering by speed so the lean stays under maxAngle. */
        bool limitSteering = true;
    };

    struct WheeledVehicle
    {
        VehicleEngine engine;
        VehicleTransmission transmission;
        float limitedSlipRatio = 1.4f;
    };

    struct MotorcycleVehicle
    {
        VehicleEngine engine = {.maxTorque = 150.0f, .minRPM = 1000.0f, .maxRPM = 10000.0f, .peakTorqueRPM = 6000.0f};
        VehicleTransmission transmission = {.gearCount = 6,
                                            .firstGear = 2.27f,
                                            .topGear = 0.88f,
                                            .reverseGear = 4.0f,
                                            .shiftUpRPM = 8000.0f,
                                            .shiftDownRPM = 2000.0f,
                                            .clutchStrength = 2.0f};
        float limitedSlipRatio = 1.4f;
        MotorcycleLean lean;
    };

    struct TrackedVehicle
    {
        VehicleEngine engine = {.minRPM = 500.0f, .maxRPM = 4000.0f, .peakTorqueRPM = 3000.0f};
        VehicleTransmission transmission = {.gearCount = 4,
                                            .firstGear = 4.0f,
                                            .topGear = 1.0f,
                                            .reverseGear = 4.0f,
                                            .shiftUpRPM = 3500.0f,
                                            .shiftDownRPM = 1000.0f};
        VehicleTrack left;
        VehicleTrack right;
    };

    enum class VehicleType : std::uint8_t
    {
        Wheeled,
        Motorcycle,
        Tracked
    };

    /** @brief The kind of vehicle, with its drivetrain: a tagged union in JointKind's pattern. */
    class VehicleKind
    {
      public:
        VehicleKind() : VehicleKind(WheeledVehicle{})
        {
        }
        VehicleKind(WheeledVehicle wheeled) : m_type(VehicleType::Wheeled), m_wheeled(wheeled)
        {
        }
        VehicleKind(MotorcycleVehicle motorcycle) : m_type(VehicleType::Motorcycle), m_motorcycle(motorcycle)
        {
        }
        VehicleKind(TrackedVehicle tracked) : m_type(VehicleType::Tracked), m_tracked(tracked)
        {
        }

        [[nodiscard]] VehicleType Type() const
        {
            return m_type;
        }

        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "VehicleKind::As: the vehicle is a different kind");
            return Member<T>(*this);
        }

        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "VehicleKind::As: the vehicle is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr VehicleType TypeOf()
        {
            if constexpr (std::is_same_v<T, WheeledVehicle>)
            {
                return VehicleType::Wheeled;
            }
            else if constexpr (std::is_same_v<T, MotorcycleVehicle>)
            {
                return VehicleType::Motorcycle;
            }
            else
            {
                static_assert(std::is_same_v<T, TrackedVehicle>,
                              "VehicleKind holds WheeledVehicle, MotorcycleVehicle or TrackedVehicle");
                return VehicleType::Tracked;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == VehicleType::Wheeled)
            {
                return self.m_wheeled;
            }
            else if constexpr (TypeOf<T>() == VehicleType::Motorcycle)
            {
                return self.m_motorcycle;
            }
            else
            {
                return self.m_tracked;
            }
        }

        VehicleType m_type;
        union {
            WheeledVehicle m_wheeled;
            MotorcycleVehicle m_motorcycle;
            TrackedVehicle m_tracked;
        };
    };

    /** @brief How each wheel finds the ground. */
    enum class VehicleProbe : std::uint8_t
    {
        Ray,
        Sphere,
        Cylinder
    };

    /** @brief Written by physics at write-back; game code's edits are overwritten. */
    struct VehicleState
    {
        /** @brief m/s along chassis -Z, signed. */
        float forwardSpeed = 0.0f;
        float engineRpm = 0.0f;
        /** @brief Engaged gear: -1 reverse, 0 neutral, 1..n. */
        int gear = 0;
        /** @brief Applied clutch, [0, 1], 0 engaged. */
        float clutch = 0.0f;
        bool shifting = false;
        int wheelsOnGround = 0;
        /** @brief Motorcycle only; radians, + right. */
        float leanAngle = 0.0f;
        /** @brief Tracked only; m/s. */
        float leftTrackSpeed = 0.0f;
        float rightTrackSpeed = 0.0f;
    };

    /**
     * @brief A dynamic RigidBody's entity is a vehicle's chassis (ADR-0015): chassis-local +Y up, -Z forward. Disabled,
     * it is a plain rigid body.
     */
    struct Vehicle
    {
        VehicleKind kind;
        VehicleInput input;
        VehicleProbe probe = VehicleProbe::Cylinder;
        /** @brief Radians: the cone around world up past which the chassis can't pitch or roll. π is off. */
        float maxTilt = std::numbers::pi_v<float>;
        bool enabled = true;
        /** @brief Read-back. */
        VehicleState state;
    };

    enum class VehicleTrackSide : std::uint8_t
    {
        Left,
        Right
    };

    struct VehicleSuspension
    {
        /** @brief Metres the wheel can rise from full extension. */
        float travel = 0.2f;
        JointSpring spring = {.frequency = 1.5f, .damping = 0.5f};
        float preload = 0.0f;
    };

    /** @brief Friction rises from 0 to its peak, falls to the slide value, and stays there. */
    struct VehicleTyre
    {
        /** @brief Longitudinal: a slip ratio; lateral: radians. */
        float peakSlip = 0.06f;
        float peakFriction = 1.2f;
        float slideSlip = 0.2f;
        float slideFriction = 1.0f;
    };

    /** @brief Written by physics at write-back; game code's edits are overwritten. */
    struct VehicleWheelState
    {
        bool onGround = false;
        Entity groundBody;
        Entity groundCollider;
        Math::Vec3 contactPoint = Math::Vec3(0.0f);
        Math::Vec3 contactNormal = Math::Vec3(0.0f);
        Math::Vec3 groundVelocity = Math::Vec3(0.0f);
        /** @brief Metres risen from full extension, 0..suspension.travel. */
        float compression = 0.0f;
        /** @brief Newtons. */
        float suspensionForce = 0.0f;
        float longitudinalSlip = 0.0f;
        /** @brief Radians. */
        float lateralSlip = 0.0f;
        /** @brief rad/s, + rolls forward. */
        float angularVelocity = 0.0f;
        /** @brief Radians, + right. */
        float steerAngle = 0.0f;
    };

    /**
     * @brief One wheel of `vehicle`, on its own entity, which physics poses for drawing (its Transform is output only:
     * world position and rotation, never scale). Wheel order is entity order.
     */
    struct VehicleWheel
    {
        Entity vehicle;
        /** @brief Chassis-local, from the chassis origin, following its world scale: the wheel centre at full
         * extension. */
        Math::Vec3 position = Math::Vec3(0.0f);
        VehicleTrackSide track = VehicleTrackSide::Left;
        float radius = 0.3f;
        float width = 0.1f;
        float inertia = 0.9f;
        float angularDamping = 0.2f;
        /** @brief Radians, mirrored by the sign of position.x. */
        float caster = 0.0f;
        float camber = 0.0f;
        float toe = 0.0f;
        VehicleSuspension suspension;
        /** @brief Radians: 0 does not steer. */
        float maxSteerAngle = 0.0f;
        float maxBrakeTorque = 1500.0f;
        float maxHandbrakeTorque = 4000.0f;
        VehicleTyre longitudinal = {.peakSlip = 0.06f, .peakFriction = 1.2f, .slideSlip = 0.2f, .slideFriction = 1.0f};
        VehicleTyre lateral = {.peakSlip = 0.052f, .peakFriction = 1.2f, .slideSlip = 0.35f, .slideFriction = 1.0f};
        /** @brief Read-back. */
        VehicleWheelState state;
    };

    /** @brief On any entity. An empty wheel is none. Adding any replaces the all-wheel-drive default. */
    struct VehicleDifferential
    {
        Entity leftWheel;
        Entity rightWheel;
        float ratio = 3.42f;
        float leftRightSplit = 0.5f;
        float limitedSlipRatio = 1.4f;
        float torqueShare = 1.0f;
    };

    /** @brief On any entity. */
    struct VehicleAntiRollBar
    {
        Entity leftWheel;
        Entity rightWheel;
        /** @brief N/m. */
        float stiffness = 1000.0f;
    };
} // namespace Fenrir
