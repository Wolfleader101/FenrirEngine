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
         * setting the direction; a negative value is taken as 0. Trailers ignore it. */
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
        /** @brief A ray down the suspension, with a fixed 80° slope limit against the world's up (against gravity). */
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
     *
     * Grip on each surface: the curve's value combines with the ground's PhysicsMaterial::friction as the geometric
     * mean, `sqrt(tyre * ground)`, as bodies combine theirs, so one `friction` means the same to bodies and tyres and
     * ice, grass and tarmac need no surface table. The ground's friction is its body's material (a compound body's is
     * its own entity's, or else its first collider's). Set ice very low: 0.05 under a 1.2 tyre still grips at 0.24.
     * The chassis's own PhysicsMaterial plays no part: it is only what bodies touching the chassis feel.
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

    /** @brief How a motorcycle keeps itself up: it leans into turns, and stands at rest. */
    struct MotorcycleLean
    {
        /** @brief Disabled, the bike falls over. A live edit. */
        bool enabled = true;
        /** @brief Radians: the most it leans, kept within 0 and a quarter turn. Changing it rebuilds the vehicle
         * privately, keeping the engine speed, the gear and the wheels' spin. */
        float maxAngle = std::numbers::pi_v<float> / 4.0f;
        /** @brief Caps steering by speed so the lean stays under `maxAngle`. A live edit. */
        bool limitSteering = true;
    };

    /**
     * @brief A motorcycle: two wheels in line, leaning into turns and standing at rest by itself through `lean`. The
     * physics backend marks its motorcycles as still in development, so a backend upgrade may change how one leans.
     *
     * Like a wheeled vehicle, it drives both wheels until game code names differentials: give it one
     * VehicleDifferential naming only its rear wheel to ride like the backend's sample bike.
     */
    struct MotorcycleVehicle
    {
        VehicleEngine engine = {.maxTorque = 150.0f, .minRpm = 1000.0f, .maxRpm = 10000.0f, .peakTorqueRpm = 6000.0f};
        VehicleTransmission transmission = {.gearCount = 6,
                                            .firstGear = 2.27f,
                                            .topGear = 0.88f,
                                            .reverseGear = 4.0f,
                                            .shiftUpRpm = 8000.0f,
                                            .shiftDownRpm = 2000.0f,
                                            .clutchStrength = 2.0f};
        /** @brief The limited-slip ratio between differentials. */
        float limitedSlipRatio = 1.4f;
        MotorcycleLean lean = {};
    };

    /**
     * @brief A trailer, wagon or hand cart: a vehicle with no engine and no gearbox, towed by an ordinary Joint. It
     * reads VehicleInput's `brake`, `handbrake` and `steer` (for a steered dolly) and ignores `forward`, `gear` and
     * `clutch`, and every VehicleDifferential: its wheels only roll, brake and steer. Its `state` engine and gear
     * fields stay 0.
     */
    struct TrailerVehicle
    {
    };

    /** @brief Which kind of vehicle a VehicleKind holds. */
    enum class VehicleType : std::uint8_t
    {
        Wheeled,
        Motorcycle,
        Trailer
    };

    /**
     * @brief A vehicle kind: exactly one kind of vehicle and its drivetrain, by value, as JointKind holds a joint.
     *
     * Assign a kind to change it (`vehicle.kind = MotorcycleVehicle{}`): a private rebuild that keeps the engine speed,
     * the gear and the wheels' spin. A trailer has no engine or gear to keep, so one turned into another kind starts
     * at idle in neutral, its wheels still spinning. Read or edit its settings through the checked view
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

        /** @brief A motorcycle. Implicit, so one can be assigned where a kind is wanted. */
        VehicleKind(MotorcycleVehicle motorcycle) : m_type(VehicleType::Motorcycle), m_motorcycle(motorcycle)
        {
        }

        /** @brief A trailer. Implicit, so one can be assigned where a kind is wanted. */
        VehicleKind(TrailerVehicle trailer) : m_type(VehicleType::Trailer), m_trailer(trailer)
        {
        }

        /** @brief The kind of vehicle held. */
        [[nodiscard]] VehicleType Type() const
        {
            return m_type;
        }

        /** @brief Whether the vehicle is of kind `T` (WheeledVehicle, MotorcycleVehicle or TrailerVehicle). */
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
                static_assert(std::is_same_v<T, TrailerVehicle>, "VehicleKind holds WheeledVehicle, "
                                                                 "MotorcycleVehicle or TrailerVehicle");
                return VehicleType::Trailer;
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
                return self.m_trailer;
            }
        }

        VehicleType m_type;
        union {
            WheeledVehicle m_wheeled;
            MotorcycleVehicle m_motorcycle;
            TrailerVehicle m_trailer;
        };
    };

    /**
     * @brief How the last collision step left a vehicle: written by physics into Vehicle::state after every step.
     * Game code's edits are overwritten. Fields a kind does not have stay 0. A sleeping vehicle keeps its last state,
     * and its wheels theirs, with their spin 0.
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
     *
     * Without a dynamic RigidBody with a solid collider, or beside a CharacterBody, it is inactive, with one warning
     * each time it becomes so: a chassis turned kinematic or static, or losing its RigidBody or collider, is a plain
     * body (if any), its `state` and its wheels' reset and its wheel entities left where they were. Valid again, it is
     * built fresh with nothing carried over, so a car made kinematic for a cutscene restarts at idle in neutral.
     *
     * Respawning is a Transform write: a teleport keeps the engine speed and wheel spin, as a body's velocity is kept,
     * and the wheels are posed on the chassis's new pose at once. A rebuild (a wheel added, removed or relinked, or the
     * chassis's body rebuilt in one tick, as when its collider is swapped) keeps the engine speed, the gear and each
     * remaining wheel's spin; a new wheel starts still. A change to the chassis's scale moves the wheels with it, with
     * no rebuild.
     *
     * What is ground is decided by layers alone: each wheel's probe filters as the chassis's CollisionFilter does,
     * finding only what the chassis would collide with, and never a trigger, the chassis itself or a character. Probes
     * ignore joints, as queries do, so a joint's `collideConnected` never hides ground. A character is never ground:
     * a vehicle meets one with its chassis, as an immovable wall, and to roll over something make it a dynamic or
     * static body. A character pushes the chassis with its CharacterBody::pushForce, as it pushes any body, which
     * barely moves a car, and one standing on the chassis rides it and presses on it with its weight. A wheel on a
     * dynamic body pushes, loads and wakes it; on moving ground its slip is measured against
     * the ground's velocity, so a vehicle parked on a kinematic platform rides it through tyre grip. Over light dynamic
     * bodies the chassis dips: an artifact of the solver's mass ratio.
     *
     * Only the chassis collides: queries and contact events see its body and name its entity, a body falling onto a
     * wheel passes through it to the chassis, and QueryFilter::ignoreBody takes the chassis entity to skip the vehicle.
     *
     * Parking is the handbrake: a vehicle at rest falls asleep as any body does, but never while `forward` is held or
     * its engine is above idle. Asleep, it keeps its last `state`, its wheels' spin 0. Any change to `input`, any edit
     * to a vehicle component (Vehicle, VehicleWheel, VehicleDifferential, VehicleAntiRollBar) and any rebuild wake the
     * chassis, so pressing the throttle always drives. Stated cost: registering any vehicle stops the physics backend
     * skipping its update of a world where everything sleeps, so a parked vehicle costs a little every tick.
     *
     * Towing is an ordinary Joint: a trailer is any dynamic body, usually a TrailerVehicle, joined to the chassis,
     * usually by a `ball` at the hitch. With the default `collideConnected = false` the hitch does not fight the
     * chassis, and the joint links the two so they sleep and wake together. A wheeled vehicle given no input also
     * tows, staying in neutral. Trailer brakes are game code's: copy the tractor's `brake` and `handbrake` into the
     * trailer's `input` each tick; nothing links them. Stated limit: a trailer much heavier than its tractor jitters at
     * the hitch.
     */
    struct Vehicle
    {
        /** @brief The kind and its drivetrain: WheeledVehicle by default. */
        VehicleKind kind = {};
        VehicleInput input = {};
        /** @brief How every wheel finds the ground. A live edit: the next step probes the new way. */
        VehicleProbe probe = VehicleProbe::Cylinder;
        /** @brief Radians: the cone around world up the chassis cannot pitch or roll past, a live edit. π (the
         * default) is off; about 60° stops rollovers. Kept within 0 to π. */
        float maxTilt = std::numbers::pi_v<float>;
        /** @brief Disabled, the entity is a plain rigid body resting on its chassis, its `state` and its wheels' reset
         * and its wheel entities left where they were. Enabled again, it is built fresh with nothing carried over. */
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
     * pose times the wheel's own: `position` raised by its compression along the suspension, then turned by its
     * camber and toe, then steered about the steering axis, then spun about its X, the axle. With no angle set its
     * rotation is the chassis's, so left and right wheels can share a symmetric mesh. The
     * Transform is output only: game code's writes to it are overwritten, and physics never reads a mount point from
     * it. Keep wheel entities unscaled and out of the chassis's hierarchy, and give each a scaled look as a child.
     *
     * Between ticks the wheels are interpolated with their chassis, under its RigidBody::interpolate and
     * PhysicsConfig::interpolate, with no switch of their own: compression, steer and spin are blended between the last
     * two steps, then posed on the chassis's blended pose, and `Tick` systems see the true pose. A teleported chassis
     * or a rebuild snaps them. With the chassis not interpolated they are shown at their step poses.
     *
     * Every setting is a live edit, applied on the next tick and waking the chassis. Out-of-range values are corrected
     * with one warning per wheel: negative inertias, frictions, brake torques, travel and widths are treated as 0, the
     * radius is raised to a small minimum, and `maxSteerAngle`, `caster`, `camber` and `toe` are kept below a quarter
     * turn. Adding, removing or relinking a wheel rebuilds its vehicle. There is no per-wheel switch: destroy the wheel
     * entity or unlink it to take the wheel off.
     *
     * A wheel naming no other entity of its scene with a Vehicle, or sharing its entity with a RigidBody, a
     * CharacterBody or any Collider (a trigger's included), is inactive: left out of its vehicle, with one warning
     * each time it becomes so, and back by itself once valid. A Joint naming a wheel entity names no body.
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
        /** @brief Radians: + tilts the top of the suspension and steering axis back (+Z), as a motorcycle's fork. */
        float caster = 0.0f;
        /** @brief Radians: + tilts the wheel's top outwards, away from the chassis's middle. Mirrored by the sign of
         * `position.x`; a wheel at x = 0 counts as a right-hand one. */
        float camber = 0.0f;
        /** @brief Radians: + turns the wheel's front inwards (toe-in). Mirrored by the sign of `position.x`; a wheel at
         * x = 0 counts as a right-hand one. */
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

    /**
     * @brief A differential: shares a vehicle's engine torque between two of its wheels (or gives one wheel its share),
     * on any entity. A vehicle with no differential naming its wheels drives every wheel equally; adding any
     * differential replaces that, and a wheel in no differential only rolls and brakes. A rear-wheel-drive car is one
     * differential naming its two rear wheels.
     *
     * An empty wheel is "none", so a differential naming one wheel drives just that wheel. A wheel that is not a wheel
     * entity, or wheels of two different vehicles, make it inactive, with a warning, until its wheels are fixed. Edits
     * apply on the next tick and wake the chassis.
     *
     * A wheel that dies while named (its entity destroyed, or its VehicleWheel removed) is treated as empty, with one
     * warning: a rear-wheel-drive car that loses a rear wheel drives the other. Limit: a differential left with no live
     * wheel names no vehicle, so if it was its vehicle's only differential, the vehicle drives every wheel again.
     */
    struct VehicleDifferential
    {
        /** @brief A wheel entity, or empty for none. */
        Entity leftWheel = {};
        /** @brief A wheel entity of the same vehicle, or empty for none. */
        Entity rightWheel = {};
        /** @brief The gearbox's turns per turn of the wheels. */
        float ratio = 3.42f;
        /** @brief How the torque is split across the two wheels: 0 all left, 0.5 even, 1 all right. */
        float leftRightSplit = 0.5f;
        /** @brief The fastest wheel's speed over the slowest's past which all the torque goes to the slower one: above
         * 1. A very large value is an open differential. */
        float limitedSlipRatio = 1.4f;
        /** @brief This differential's share of the engine torque, against the other differentials of its vehicle:
         * the shares are normalised, so they need not sum to 1. */
        float torqueShare = 1.0f;
    };

    /**
     * @brief An anti-roll bar: stops a vehicle leaning in corners by pushing its two wheels' suspensions towards the
     * same compression, on any entity. A vehicle has none by default. It works only while both wheels are on the
     * ground.
     *
     * A wheel that is not a wheel entity, or wheels of two different vehicles, make it inactive, with a warning, until
     * its wheels are fixed; with a wheel empty it does nothing. A wheel that dies while named (its entity destroyed, or
     * its VehicleWheel removed) is treated as empty, with one warning. Edits apply on the next tick and wake the
     * chassis.
     */
    struct VehicleAntiRollBar
    {
        /** @brief A wheel entity, usually on the vehicle's left. */
        Entity leftWheel = {};
        /** @brief A wheel entity of the same vehicle, usually the left wheel's partner on the right. */
        Entity rightWheel = {};
        /** @brief N/m: the push per metre of difference in the two wheels' compression. 0 does nothing. */
        float stiffness = 1000.0f;
    };
} // namespace Fenrir
