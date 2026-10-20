#pragma once

#include <cassert>
#include <cstdint>
#include <numbers>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Joints.hpp"

namespace Fenrir
{
    /**
     * @brief Driver input: what game code sets on a vehicle to drive it, normalised. Game code writes it in `Tick`, and
     * physics applies it in `PostTick`, clamped silently to each field's range. It persists until game code changes it;
     * physics never writes it, so what game code set is what it reads. Any change to it wakes the chassis.
     */
    struct VehicleInput
    {
        /** @brief [-1, 1]: how hard to drive, negative in reverse (Automatic). In Manual a pedal, [0, 1], the gear
         * setting the direction; a negative value is taken as 0. */
        float forward = 0.0f;
        /** @brief [-1, 1], + right. Tracked vehicles ignore it. */
        float steer = 0.0f;
        /** @brief [0, 1]. */
        float brake = 0.0f;
        /** @brief [0, 1]. Tracked vehicles ignore it. */
        float handbrake = 0.0f;
        /** @brief Tracked only: the left track's speed ratio, [-1, 1]; 0 holds the track still. */
        float leftTrack = 1.0f;
        /** @brief Tracked only: the right track's speed ratio, [-1, 1]; 0 holds the track still. */
        float rightTrack = 1.0f;
        /** @brief Manual only: -1 reverse, 0 neutral, 1..n forward, clamped to the gearbox's gears. */
        int gear = 0;
        /** @brief Manual only: the clutch pedal, [0, 1], 0 engaged. */
        float clutch = 0.0f;
    };

    /** @brief How every wheel of a vehicle finds the ground beneath it. */
    enum class VehicleProbe : std::uint8_t
    {
        /** @brief A ray down the suspension, with a fixed 80° slope limit against the world's up. */
        Ray,
        /** @brief A sphere cast down the suspension, with the same slope limit. */
        Sphere,
        /** @brief A cylinder the wheel's size cast down the suspension, with no slope limit: wheels roll over kerbs. */
        Cylinder
    };

    /** @brief A wheel's suspension: how far it can rise, and how hard it pushes back. */
    struct VehicleSuspension
    {
        /** @brief Metres: the suspension travel, how far the wheel can rise from full extension. 0 is rigid. */
        float travel = 0.2f;
        /**
         * @brief The spring, in hertz and a damping ratio; a frequency of 0 is rigid. JointSpring's own damping
         * default is 1.0, not the suspension's 0.5, so `.spring = {.frequency = 2.0f}` gives damping 1.0: write both.
         */
        JointSpring spring = {.frequency = 1.5f, .damping = 0.5f};
        /** @brief Metres the spring is compressed at full extension, so it pushes from the start. */
        float preload = 0.0f;
    };

    /**
     * @brief A tyre's friction against its slip: it rises from 0 at no slip to `peakFriction` at `peakSlip`, falls to
     * `slideFriction` at `slideSlip`, and stays there. Along the wheel slip is a ratio, across it an angle in radians.
     * The value is combined with the ground's PhysicsMaterial::friction as bodies combine theirs.
     */
    struct VehicleTyre
    {
        float peakSlip = 0.0f;
        float peakFriction = 0.0f;
        float slideSlip = 0.0f;
        float slideFriction = 0.0f;
    };

    /** @brief A vehicle's engine. Its torque curve is built from the shape fields; the engine never stalls. */
    struct VehicleEngine
    {
        /** @brief Newton metres, at `peakTorqueRpm`. */
        float maxTorque = 500.0f;
        /** @brief The idle speed, in RPM. */
        float minRpm = 1000.0f;
        float maxRpm = 6000.0f;
        float peakTorqueRpm = 4000.0f;
        /** @brief The fraction of `maxTorque` at `minRpm`. */
        float torqueAtMinRpm = 0.8f;
        /** @brief The fraction of `maxTorque` at `maxRpm`. */
        float torqueAtMaxRpm = 0.8f;
        /** @brief kg m². */
        float inertia = 0.5f;
        float angularDamping = 0.2f;
    };

    /** @brief Who changes gear: the gearbox itself, or game code through VehicleInput::gear and `clutch`. */
    enum class TransmissionMode : std::uint8_t
    {
        Automatic,
        Manual
    };

    /**
     * @brief A vehicle's gearbox: `gearCount` forward gears spaced geometrically from `firstGear` to `topGear`, and one
     * reverse gear. The shift and switch fields are read only in Automatic.
     */
    struct VehicleTransmission
    {
        TransmissionMode mode = TransmissionMode::Automatic;
        int gearCount = 5;
        float firstGear = 2.66f;
        float topGear = 0.74f;
        /** @brief The reverse gear's ratio, as a positive magnitude. */
        float reverseGear = 2.90f;
        /** @brief RPM. */
        float shiftUpRpm = 4000.0f;
        /** @brief RPM. */
        float shiftDownRpm = 2000.0f;
        /** @brief Seconds a shift takes. */
        float switchTime = 0.5f;
        /** @brief Seconds the clutch takes to engage after a shift. */
        float clutchReleaseTime = 0.3f;
        /** @brief Seconds after a shift before the next. */
        float switchLatency = 0.5f;
        float clutchStrength = 10.0f;
    };

    /**
     * @brief A wheeled vehicle (a car, a truck): steered by turning wheels, driving every wheel equally through its
     * gearbox until game code names differentials.
     */
    struct WheeledVehicle
    {
        VehicleEngine engine = {};
        VehicleTransmission transmission = {};
        /** @brief The limited-slip ratio between differentials. */
        float limitedSlipRatio = 1.4f;
    };

    /** @brief Which kind of vehicle a VehicleKind holds. */
    enum class VehicleType : std::uint8_t
    {
        Wheeled
    };

    /**
     * @brief A vehicle kind: exactly one kind of vehicle and its drivetrain, by value, as JointKind holds a joint.
     *
     * Assign a kind to change it (`vehicle.kind = WheeledVehicle{}`); read or edit its settings through the checked
     * view
     * (`vehicle.kind.As<WheeledVehicle>()`). Reading it as a kind it does not hold asserts.
     */
    class VehicleKind
    {
      public:
        /** @brief A wheeled vehicle (`WheeledVehicle{}`). */
        VehicleKind() : VehicleKind(WheeledVehicle{})
        {
        }

        /** @brief A wheeled vehicle. Implicit, so one can be assigned where a kind is wanted. */
        VehicleKind(WheeledVehicle wheeled) : m_type(VehicleType::Wheeled), m_wheeled(wheeled)
        {
        }

        /** @brief The kind of vehicle held. */
        [[nodiscard]] VehicleType Type() const
        {
            return m_type;
        }

        /** @brief Whether the vehicle is of kind `T` (WheeledVehicle). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The vehicle as kind `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "VehicleKind::As: the vehicle is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
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
            static_assert(std::is_same_v<T, WheeledVehicle>, "VehicleKind holds WheeledVehicle");
            return VehicleType::Wheeled;
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            return self.m_wheeled;
        }

        VehicleType m_type;
        union {
            WheeledVehicle m_wheeled;
        };
    };

    /**
     * @brief How the last collision step left a vehicle: written by physics into Vehicle::state after every step.
     * Game code's edits are overwritten. Fields a kind does not have stay 0.
     */
    struct VehicleState
    {
        /** @brief m/s along the chassis's -Z (forward), signed. */
        float forwardSpeed = 0.0f;
        float engineRpm = 0.0f;
        /** @brief The engaged gear, numbered as VehicleInput::gear: -1 reverse, 0 neutral, 1..n forward. In Automatic
         * the gearbox's own choice: it drops to neutral braking to a stop. */
        int gear = 0;
        /** @brief The applied clutch, [0, 1], 0 engaged. */
        float clutch = 0.0f;
        /** @brief Changing gear now. */
        bool shifting = false;
        int wheelsOnGround = 0;
        /** @brief Motorcycle only: radians, + right. */
        float leanAngle = 0.0f;
        /** @brief Tracked only: m/s. */
        float leftTrackSpeed = 0.0f;
        /** @brief Tracked only: m/s. */
        float rightTrackSpeed = 0.0f;
    };

    /**
     * @brief Makes the entity's dynamic RigidBody a vehicle (ADR-0015): the body is its chassis, the only part of the
     * vehicle that collides. Its wheels are VehicleWheel components on wheel entities of their own, each naming this
     * entity. Chassis-local +Y is up and -Z is forward, by convention: turn a model facing another way on a child.
     *
     * Game code drives it by writing `input` in `Tick`. With every default, a wheeled vehicle drives every wheel
     * through an automatic five-speed gearbox.
     *
     * Plain data that physics compares each tick with what it last applied (ADR-0011). A vehicle with no wheels is
     * valid but inert. Its body needs a collider it can make, as any body does; the chassis must not be scaled to size
     * its look: give it an unscaled BoxShape of the right size and a scaled look as a child.
     */
    struct Vehicle
    {
        /** @brief The kind and its drivetrain: WheeledVehicle by default. */
        VehicleKind kind = {};
        VehicleInput input = {};
        VehicleProbe probe = VehicleProbe::Cylinder;
        /** @brief Radians: the cone around world up the chassis cannot pitch or roll past. π (the default) is off;
         * about 60° stops rollovers. */
        float maxTilt = std::numbers::pi_v<float>;
        /** @brief Disabled, the entity is a plain rigid body resting on its chassis. */
        bool enabled = true;
        /** @brief Read-back: overwritten by physics after every step. */
        VehicleState state = {};
    };

    /** @brief Which of a tracked vehicle's two tracks a wheel sits on. */
    enum class VehicleTrackSide : std::uint8_t
    {
        Left,
        Right
    };

    /**
     * @brief How the last collision step left a wheel: written by physics into VehicleWheel::state after every step,
     * from that collision step's probes against the chassis's pose after it, so contact, compression and steer lag one
     * collision step. Game code's edits are overwritten. Off the ground, the ground fields are empty or 0.
     *
     * A skid is game code's to judge: compare the slips with the tyre's `peakSlip` and `slideSlip`.
     */
    struct VehicleWheelState
    {
        bool onGround = false;
        /** @brief The body entity of the ground, on ground only. A handle: destroyed since, it is no longer valid. */
        Entity groundBody = {};
        /** @brief The collider entity of the ground touched, on ground only. */
        Entity groundCollider = {};
        /** @brief World space. */
        Math::Vec3 contactPoint = Math::Vec3(0.0f);
        /** @brief World space, unit length. */
        Math::Vec3 contactNormal = Math::Vec3(0.0f);
        /** @brief m/s, the ground's velocity at the contact. */
        Math::Vec3 groundVelocity = Math::Vec3(0.0f);
        /** @brief Metres risen from full extension: 0 to `suspension.travel`. */
        float compression = 0.0f;
        /** @brief Newtons. */
        float suspensionForce = 0.0f;
        /** @brief Along the wheel, a ratio in VehicleTyre's terms. */
        float longitudinalSlip = 0.0f;
        /** @brief Across the wheel, radians. */
        float lateralSlip = 0.0f;
        /** @brief rad/s, + rolls forward. */
        float angularVelocity = 0.0f;
        /** @brief Radians, + right, as VehicleInput::steer. */
        float steerAngle = 0.0f;
    };

    /**
     * @brief One wheel of a vehicle, on its own wheel entity, naming the vehicle's chassis. It is never a body and
     * nothing collides with it. Wheel order is entity order.
     *
     * Physics poses the wheel entity for drawing: its world position and rotation (never its scale) are the chassis's
     * pose times the wheel's own, `position` raised by its compression, then steered, then spun about its X, the axle.
     * With no angle set its rotation is the chassis's, so left and right wheels can share a symmetric mesh. The
     * Transform is output only: game code's writes to it are overwritten, and physics never reads a mount point from
     * it. Keep wheel entities unscaled and out of the chassis's hierarchy, and give each a scaled look as a child.
     *
     * Adding, removing or relinking a wheel rebuilds its vehicle. There is no per-wheel switch: destroy the wheel
     * entity or unlink it to take the wheel off.
     */
    struct VehicleWheel
    {
        /** @brief The chassis entity: the entity with the Vehicle. */
        Entity vehicle = {};
        /** @brief Chassis-local metres from the chassis origin, scaled with the chassis: the wheel's centre at full
         * extension. */
        Math::Vec3 position = Math::Vec3(0.0f);
        /** @brief Tracked vehicles only. */
        VehicleTrackSide track = VehicleTrackSide::Left;
        /** @brief Metres, never scaled. */
        float radius = 0.3f;
        /** @brief Metres, never scaled. */
        float width = 0.1f;
        /** @brief kg m². */
        float inertia = 0.9f;
        float angularDamping = 0.2f;
        /** @brief Radians: tilts the suspension and steering axis back. Mirrored by the sign of `position.x`. */
        float caster = 0.0f;
        /** @brief Radians: tilts the wheel's top. Mirrored by the sign of `position.x`. */
        float camber = 0.0f;
        /** @brief Radians: turns the wheel's front. Mirrored by the sign of `position.x`. */
        float toe = 0.0f;
        VehicleSuspension suspension = {};
        /** @brief Radians at full `steer`: 0 does not steer. */
        float maxSteerAngle = 0.0f;
        /** @brief Newton metres at full `brake`. */
        float maxBrakeTorque = 1500.0f;
        /** @brief Newton metres at full `handbrake`. */
        float maxHandbrakeTorque = 4000.0f;
        VehicleTyre longitudinal = {.peakSlip = 0.06f, .peakFriction = 1.2f, .slideSlip = 0.2f, .slideFriction = 1.0f};
        VehicleTyre lateral = {.peakSlip = 0.052f, .peakFriction = 1.2f, .slideSlip = 0.35f, .slideFriction = 1.0f};
        /** @brief Read-back: overwritten by physics after every step. */
        VehicleWheelState state = {};
    };
} // namespace Fenrir
