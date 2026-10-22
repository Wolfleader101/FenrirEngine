// Vehicles' settings (ADR-0015): the diff's comparisons, the corrections of out-of-range values and the backend
// descriptions built from a vehicle's components. Knows the components, not Jolt.

#include "VehicleSettings.hpp"

#include <algorithm>
#include <cstdint>
#include <numbers>
#include <vector>

#include "FenrirLogger/ILogger.hpp"
#include "OutOfRange.hpp"
#include "Same.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        bool Same(const JointSpring& a, const JointSpring& b)
        {
            return Fenrir::Same(a.frequency, b.frequency) && Fenrir::Same(a.damping, b.damping);
        }

        bool Same(const VehicleTyre& a, const VehicleTyre& b)
        {
            return Fenrir::Same(a.peakSlip, b.peakSlip) && Fenrir::Same(a.peakFriction, b.peakFriction) &&
                   Fenrir::Same(a.slideSlip, b.slideSlip) && Fenrir::Same(a.slideFriction, b.slideFriction);
        }

        bool Same(const VehicleEngine& a, const VehicleEngine& b)
        {
            return Fenrir::Same(a.maxTorque, b.maxTorque) && Fenrir::Same(a.minRpm, b.minRpm) &&
                   Fenrir::Same(a.maxRpm, b.maxRpm) && Fenrir::Same(a.peakTorqueRpm, b.peakTorqueRpm) &&
                   Fenrir::Same(a.torqueAtMinRpm, b.torqueAtMinRpm) &&
                   Fenrir::Same(a.torqueAtMaxRpm, b.torqueAtMaxRpm) && Fenrir::Same(a.inertia, b.inertia) &&
                   Fenrir::Same(a.angularDamping, b.angularDamping);
        }

        bool Same(const VehicleTransmission& a, const VehicleTransmission& b)
        {
            return a.mode == b.mode && a.gearCount == b.gearCount && Fenrir::Same(a.firstGear, b.firstGear) &&
                   Fenrir::Same(a.topGear, b.topGear) && Fenrir::Same(a.reverseGear, b.reverseGear) &&
                   Fenrir::Same(a.shiftUpRpm, b.shiftUpRpm) && Fenrir::Same(a.shiftDownRpm, b.shiftDownRpm) &&
                   Fenrir::Same(a.switchTime, b.switchTime) && Fenrir::Same(a.clutchReleaseTime, b.clutchReleaseTime) &&
                   Fenrir::Same(a.switchLatency, b.switchLatency) && Fenrir::Same(a.clutchStrength, b.clutchStrength);
        }

        bool Same(const VehicleTrack& a, const VehicleTrack& b)
        {
            return Fenrir::Same(a.inertia, b.inertia) && Fenrir::Same(a.angularDamping, b.angularDamping) &&
                   Fenrir::Same(a.maxBrakeTorque, b.maxBrakeTorque) &&
                   Fenrir::Same(a.differentialRatio, b.differentialRatio) &&
                   Fenrir::Same(a.longitudinalFriction, b.longitudinalFriction) &&
                   Fenrir::Same(a.lateralFriction, b.lateralFriction);
        }

        bool Same(const Backend::VehicleDriveDesc& a, const Backend::VehicleDriveDesc& b)
        {
            return a.controller == b.controller && a.powered == b.powered && Same(a.engine, b.engine) &&
                   Same(a.transmission, b.transmission) && Fenrir::Same(a.limitedSlipRatio, b.limitedSlipRatio) &&
                   a.lean.enabled == b.lean.enabled && Fenrir::Same(a.lean.maxAngle, b.lean.maxAngle) &&
                   a.lean.limitSteering == b.lean.limitSteering && Same(a.leftTrack, b.leftTrack) &&
                   Same(a.rightTrack, b.rightTrack);
        }

        bool Same(const Backend::VehicleDifferentialDesc& a, const Backend::VehicleDifferentialDesc& b)
        {
            return a.leftWheel == b.leftWheel && a.rightWheel == b.rightWheel && Fenrir::Same(a.ratio, b.ratio) &&
                   Fenrir::Same(a.leftRightSplit, b.leftRightSplit) &&
                   Fenrir::Same(a.limitedSlipRatio, b.limitedSlipRatio) && Fenrir::Same(a.torqueShare, b.torqueShare);
        }

        bool Same(const Backend::VehicleAntiRollBarDesc& a, const Backend::VehicleAntiRollBarDesc& b)
        {
            return a.leftWheel == b.leftWheel && a.rightWheel == b.rightWheel && Fenrir::Same(a.stiffness, b.stiffness);
        }

        template <typename T>
        bool SameAll(const std::vector<T>& a, const std::vector<T>& b)
        {
            return std::ranges::equal(a, b, [](const T& x, const T& y) { return Same(x, y); });
        }

        // Just below a quarter turn: the most a wheel steers or leans.
        constexpr float maxAngle = 0.4999f * std::numbers::pi_v<float>;
        constexpr float minRadius = 0.01f;
        // The backend divides by a wheel's inertia: 0, as written or corrected, is this little.
        constexpr float minInertia = 1e-3f;
        // Just above 1: a limited-slip ratio of 1 or less locks the differential.
        constexpr float minLimitedSlipRatio = 1.001f;

        // The kinds of out-of-range value a wheel can hold, each warned about once per wheel.
        enum WheelProblem : std::uint32_t
        {
            NegativeSize = 1u << 0,
            SmallRadius = 1u << 1,
            NegativeWheelInertia = 1u << 2,
            NegativeDamping = 1u << 3,
            NegativeSuspension = 1u << 4,
            NegativeBrakeTorque = 1u << 5,
            AngleTooLarge = 1u << 6,
            NegativeFriction = 1u << 7,
            BadSlip = 1u << 8,
        };

        // A wheel's out-of-range settings corrected into the ranges the backend can use.
        VehicleWheel Corrected(const VehicleWheel& wheel, OutOfRange& check)
        {
            VehicleWheel corrected = wheel;
            check.NotNegative(corrected.width, NegativeSize, "a negative width is treated as 0");
            if (corrected.radius < minRadius)
            {
                corrected.radius = minRadius;
                check.Warn(SmallRadius, "a radius below 1 cm is raised to it");
            }
            check.NotNegative(corrected.inertia, NegativeWheelInertia, "a negative inertia is treated as 0");
            corrected.inertia = std::max(corrected.inertia, minInertia);
            check.NotNegative(corrected.angularDamping, NegativeDamping, "a negative angular damping is treated as 0");
            VehicleSuspension& suspension = corrected.suspension;
            for (float* value :
                 {&suspension.travel, &suspension.preload, &suspension.spring.frequency, &suspension.spring.damping})
            {
                check.NotNegative(*value, NegativeSuspension,
                                  "a negative suspension travel, preload, frequency or damping is treated as 0");
            }
            for (float* value : {&corrected.maxBrakeTorque, &corrected.maxHandbrakeTorque})
            {
                check.NotNegative(*value, NegativeBrakeTorque, "a negative brake torque is treated as 0");
            }
            for (float* angle : {&corrected.maxSteerAngle, &corrected.caster, &corrected.camber, &corrected.toe})
            {
                check.Within(*angle, -maxAngle, maxAngle, AngleTooLarge,
                             "a maxSteerAngle, caster, camber or toe of a quarter turn or more is kept below it");
            }
            for (VehicleTyre* tyre : {&corrected.longitudinal, &corrected.lateral})
            {
                check.NotNegative(tyre->peakFriction, NegativeFriction, "a negative tyre friction is treated as 0");
                check.NotNegative(tyre->slideFriction, NegativeFriction, "a negative tyre friction is treated as 0");
                check.NotNegative(tyre->peakSlip, BadSlip, "a negative peak slip is treated as 0");
                if (tyre->slideSlip <= tyre->peakSlip)
                {
                    tyre->slideSlip = tyre->peakSlip + 1e-4f;
                    check.Warn(BadSlip, "a slide slip not past the peak slip is moved just past it");
                }
            }
            return corrected;
        }

        // A wheel for the backend, from its corrected settings and its position with the chassis's scale applied.
        // Caster tilts the steering axis's top back (+Z), camber the wheel's top out and toe its front in, mirrored
        // for a wheel on the left (x < 0).
        Backend::VehicleWheelDesc DescOf(const VehicleWheel& corrected, const Math::Vec3& position)
        {
            const float side = position.x < 0.0f ? -1.0f : 1.0f;
            return Backend::VehicleWheelDesc{
                .position = position,
                .steeringAxis =
                    Math::AngleAxis(corrected.caster, Math::Vec3(1.0f, 0.0f, 0.0f)) * Math::Vec3(0.0f, 1.0f, 0.0f),
                .rest = Math::AngleAxis(side * corrected.toe, Math::Vec3(0.0f, 1.0f, 0.0f)) *
                        Math::AngleAxis(-side * corrected.camber, Math::Vec3(0.0f, 0.0f, 1.0f)),
                .radius = corrected.radius,
                .width = corrected.width,
                .suspension = corrected.suspension,
                .inertia = corrected.inertia,
                .angularDamping = corrected.angularDamping,
                .maxSteerAngle = corrected.maxSteerAngle,
                .maxBrakeTorque = corrected.maxBrakeTorque,
                .maxHandbrakeTorque = corrected.maxHandbrakeTorque,
                .longitudinal = corrected.longitudinal,
                .lateral = corrected.lateral,
                .track = corrected.track};
        }

        // The engine's inertia an inertia of 0 is raised to: the backend divides by it.
        constexpr float minEngineInertia = 1e-3f;

        // The kinds of out-of-range value a vehicle's kind can hold, each warned about once per vehicle while it holds
        // one. Found in this order, so a correction finding several warns in it.
        enum KindProblem : std::uint32_t
        {
            NegativeTorque = 1u << 0,
            NegativeInertia = 1u << 1,
            EmptyRpmRange = 1u << 2,
            PeakTorqueOutOfRange = 1u << 3,
            ShiftPointOutOfRange = 1u << 4,
            ShiftDownNotBelowShiftUp = 1u << 5,
            NoGears = 1u << 6,
            LimitedSlipAtMostOne = 1u << 7,
            NegativeTrackValue = 1u << 8,
        };

        // An engine and gearbox corrected into the ranges the backend can use. Values the spec does not name as out of
        // range are corrected silently, just enough to keep the backend's asserts happy.
        void Correct(VehicleEngine& engine, VehicleTransmission& transmission, OutOfRange& check)
        {
            for (float* torque : {&engine.maxTorque, &engine.torqueAtMinRpm, &engine.torqueAtMaxRpm})
            {
                check.NotNegative(*torque, NegativeTorque, "a negative torque is treated as 0");
            }
            check.NotNegative(engine.inertia, NegativeInertia, "a negative engine inertia is treated as 0");
            engine.inertia = std::max(engine.inertia, minEngineInertia);
            for (float* value : {&engine.minRpm, &engine.angularDamping, &transmission.switchTime,
                                 &transmission.clutchReleaseTime, &transmission.switchLatency})
            {
                *value = std::max(*value, 0.0f);
            }

            const auto outside = [&engine](float rpm) { return rpm < engine.minRpm || rpm > engine.maxRpm; };
            if (engine.maxRpm <= engine.minRpm)
            {
                check.Warn(EmptyRpmRange, "a maximum RPM at or below the minimum is raised above it");
            }
            engine.maxRpm = std::max(engine.maxRpm, engine.minRpm + 1.0f);
            check.Within(engine.peakTorqueRpm, engine.minRpm, engine.maxRpm, PeakTorqueOutOfRange,
                         "a peak torque RPM outside the RPM range is moved into it");

            for (float* ratio : {&transmission.firstGear, &transmission.topGear, &transmission.reverseGear})
            {
                *ratio = std::max(*ratio, 1e-3f);
            }
            // Shifting up below the top of the range and down above the bottom, with room between.
            if (outside(transmission.shiftUpRpm) || outside(transmission.shiftDownRpm))
            {
                check.Warn(ShiftPointOutOfRange, "a shift point outside the RPM range is moved into it");
            }
            transmission.shiftUpRpm = std::clamp(transmission.shiftUpRpm, engine.minRpm + 0.5f, engine.maxRpm - 0.5f);
            transmission.shiftDownRpm = std::max(transmission.shiftDownRpm, std::max(engine.minRpm, 0.5f) - 0.25f);
            if (transmission.shiftDownRpm >= transmission.shiftUpRpm)
            {
                check.Warn(ShiftDownNotBelowShiftUp,
                           "a shift-down RPM at or above the shift-up RPM is lowered below it");
            }
            transmission.shiftDownRpm = std::min(transmission.shiftDownRpm, transmission.shiftUpRpm - 0.25f);
            transmission.clutchStrength = std::max(transmission.clutchStrength, 1e-3f);
            // After the shift points, which do not read it, so the warnings come in KindProblem's order.
            if (transmission.gearCount < 1)
            {
                transmission.gearCount = 1;
                check.Warn(NoGears, "a gear count below 1 is raised to 1");
            }
        }

        // The least inertia and differential ratio a track can have: the backend divides by both.
        constexpr float minTrackInertia = 1e-3f;
        constexpr float minTrackRatio = 1e-3f;

        // A track corrected into the ranges the backend can use.
        void Correct(VehicleTrack& track, OutOfRange& check)
        {
            for (float* value :
                 {&track.inertia, &track.maxBrakeTorque, &track.longitudinalFriction, &track.lateralFriction})
            {
                check.NotNegative(*value, NegativeTrackValue,
                                  "a negative track inertia, brake torque or friction is treated as 0");
            }
            track.inertia = std::max(track.inertia, minTrackInertia);
            track.angularDamping = std::max(track.angularDamping, 0.0f);
            track.differentialRatio = std::max(track.differentialRatio, minTrackRatio);
        }

        // The kinds of out-of-range value a differential or anti-roll bar can hold, each warned about once per
        // component.
        enum LinkProblem : std::uint32_t
        {
            LimitedSlipAtOrBelowOne = 1u << 0,
            RatioNotPositive = 1u << 1,
            SplitOutOfRange = 1u << 2,
            NegativeTorqueShare = 1u << 3,
            NegativeStiffness = 1u << 4,
        };

        // Just above 0: the least gearbox-to-wheel ratio a differential can have.
        constexpr float minDifferentialRatio = 1e-3f;

        // How a kind of vehicle is driven. A trailer is unpowered: the default engine and a Manual gearbox, which
        // Clamped holds in neutral, so nothing drives its wheels.
        Backend::VehicleDriveDesc DriveOf(const VehicleKind& kind)
        {
            if (kind.Is<TrailerVehicle>())
            {
                return Backend::VehicleDriveDesc{.powered = false, .transmission = {.mode = TransmissionMode::Manual}};
            }
            if (kind.Is<MotorcycleVehicle>())
            {
                const MotorcycleVehicle& motorcycle = kind.As<MotorcycleVehicle>();
                return Backend::VehicleDriveDesc{.controller = Backend::VehicleController::Motorcycle,
                                                 .engine = motorcycle.engine,
                                                 .transmission = motorcycle.transmission,
                                                 .limitedSlipRatio = motorcycle.limitedSlipRatio,
                                                 .lean = motorcycle.lean};
            }
            if (kind.Is<TrackedVehicle>())
            {
                const TrackedVehicle& tracked = kind.As<TrackedVehicle>();
                return Backend::VehicleDriveDesc{.controller = Backend::VehicleController::Tracked,
                                                 .engine = tracked.engine,
                                                 .transmission = tracked.transmission,
                                                 .leftTrack = tracked.left,
                                                 .rightTrack = tracked.right};
            }
            const WheeledVehicle& wheeled = kind.As<WheeledVehicle>();
            return Backend::VehicleDriveDesc{.engine = wheeled.engine,
                                             .transmission = wheeled.transmission,
                                             .limitedSlipRatio = wheeled.limitedSlipRatio};
        }
    } // namespace

    bool Same(const VehicleWheel& a, const VehicleWheel& b)
    {
        return Fenrir::Same(a.position, b.position) && a.track == b.track && Fenrir::Same(a.radius, b.radius) &&
               Fenrir::Same(a.width, b.width) && Fenrir::Same(a.inertia, b.inertia) &&
               Fenrir::Same(a.angularDamping, b.angularDamping) && Fenrir::Same(a.caster, b.caster) &&
               Fenrir::Same(a.camber, b.camber) && Fenrir::Same(a.toe, b.toe) &&
               Fenrir::Same(a.suspension.travel, b.suspension.travel) &&
               Same(a.suspension.spring, b.suspension.spring) &&
               Fenrir::Same(a.suspension.preload, b.suspension.preload) &&
               Fenrir::Same(a.maxSteerAngle, b.maxSteerAngle) && Fenrir::Same(a.maxBrakeTorque, b.maxBrakeTorque) &&
               Fenrir::Same(a.maxHandbrakeTorque, b.maxHandbrakeTorque) && Same(a.longitudinal, b.longitudinal) &&
               Same(a.lateral, b.lateral);
    }

    bool Same(const VehicleInput& a, const VehicleInput& b)
    {
        return Fenrir::Same(a.forward, b.forward) && Fenrir::Same(a.steer, b.steer) && Fenrir::Same(a.brake, b.brake) &&
               Fenrir::Same(a.handbrake, b.handbrake) && Fenrir::Same(a.leftTrack, b.leftTrack) &&
               Fenrir::Same(a.rightTrack, b.rightTrack) && a.gear == b.gear && Fenrir::Same(a.clutch, b.clutch);
    }

    bool Same(const VehicleKind& a, const VehicleKind& b)
    {
        return a.Type() == b.Type() && Same(DriveOf(a), DriveOf(b));
    }

    bool Same(const std::vector<Backend::VehicleDifferentialDesc>& a,
              const std::vector<Backend::VehicleDifferentialDesc>& b)
    {
        return SameAll(a, b);
    }

    bool Same(const std::vector<Backend::VehicleAntiRollBarDesc>& a,
              const std::vector<Backend::VehicleAntiRollBarDesc>& b)
    {
        return SameAll(a, b);
    }

    VehicleInput Clamped(const VehicleInput& input, const Backend::VehicleDriveDesc& drive)
    {
        const bool manual = drive.transmission.mode == TransmissionMode::Manual;
        VehicleInput clamped = input;
        // An unpowered vehicle has no engine or gearbox to drive.
        if (!drive.powered)
        {
            clamped.forward = 0.0f;
            clamped.gear = 0;
            clamped.clutch = 0.0f;
        }
        clamped.forward = std::clamp(clamped.forward, manual ? 0.0f : -1.0f, 1.0f);
        clamped.steer = std::clamp(input.steer, -1.0f, 1.0f);
        clamped.brake = std::clamp(input.brake, 0.0f, 1.0f);
        clamped.handbrake = std::clamp(input.handbrake, 0.0f, 1.0f);
        clamped.leftTrack = std::clamp(input.leftTrack, -1.0f, 1.0f);
        clamped.rightTrack = std::clamp(input.rightTrack, -1.0f, 1.0f);
        clamped.gear = std::clamp(clamped.gear, -1, drive.transmission.gearCount);
        clamped.clutch = std::clamp(clamped.clutch, 0.0f, 1.0f);
        return clamped;
    }

    float TiltOf(const Vehicle& vehicle)
    {
        return std::clamp(vehicle.maxTilt, 0.0f, std::numbers::pi_v<float>);
    }

    Backend::VehicleDriveDesc DriveDescOf(const VehicleKind& kind, std::uint32_t entity, std::uint32_t& warned,
                                          ILogger& logger)
    {
        OutOfRange check("Vehicle", entity, warned, logger, OutOfRange::Lifetime::UntilValid);
        VehicleKind corrected = kind;
        const auto correct = [&check](auto& drivetrain) {
            Correct(drivetrain.engine, drivetrain.transmission, check);
            if (drivetrain.limitedSlipRatio <= 1.0f)
            {
                drivetrain.limitedSlipRatio = minLimitedSlipRatio;
                check.Warn(LimitedSlipAtMostOne, "a limited-slip ratio at or below 1 is raised to just above it");
            }
        };
        if (corrected.Is<MotorcycleVehicle>())
        {
            MotorcycleVehicle& motorcycle = corrected.As<MotorcycleVehicle>();
            correct(motorcycle);
            // Silently, as the tilt limit is: the backend takes its tangent.
            motorcycle.lean.maxAngle = std::clamp(motorcycle.lean.maxAngle, 0.0f, maxAngle);
        }
        else if (corrected.Is<TrackedVehicle>())
        {
            TrackedVehicle& tracked = corrected.As<TrackedVehicle>();
            Correct(tracked.engine, tracked.transmission, check);
            Correct(tracked.left, check);
            Correct(tracked.right, check);
        }
        else if (corrected.Is<WheeledVehicle>())
        {
            correct(corrected.As<WheeledVehicle>());
        }
        return DriveOf(corrected);
    }

    Backend::VehicleWheelDesc WheelDescOf(std::uint32_t entity, const VehicleWheel& wheel, const Math::Vec3& scale,
                                          std::uint32_t& warned, ILogger& logger)
    {
        OutOfRange check("VehicleWheel", entity, warned, logger, OutOfRange::Lifetime::PerComponent);
        return DescOf(Corrected(wheel, check), wheel.position * scale);
    }

    Backend::VehicleDifferentialDesc DifferentialDescOf(std::uint32_t entity, const VehicleDifferential& differential,
                                                        int left, int right, std::uint32_t& warned, ILogger& logger)
    {
        OutOfRange check("VehicleDifferential", entity, warned, logger, OutOfRange::Lifetime::PerComponent);
        Backend::VehicleDifferentialDesc desc{.leftWheel = left,
                                              .rightWheel = right,
                                              .ratio = differential.ratio,
                                              .leftRightSplit = differential.leftRightSplit,
                                              .limitedSlipRatio = differential.limitedSlipRatio,
                                              .torqueShare = differential.torqueShare};
        // Written to catch NaN too.
        if (!(desc.limitedSlipRatio >= minLimitedSlipRatio))
        {
            desc.limitedSlipRatio = minLimitedSlipRatio;
            check.Warn(LimitedSlipAtOrBelowOne, "a limited-slip ratio at or below 1 is raised to just above it");
        }
        if (!(desc.ratio >= minDifferentialRatio))
        {
            desc.ratio = minDifferentialRatio;
            check.Warn(RatioNotPositive, "a ratio at or below 0 is raised to a small minimum");
        }
        if (!(desc.leftRightSplit >= 0.0f && desc.leftRightSplit <= 1.0f))
        {
            desc.leftRightSplit = desc.leftRightSplit > 1.0f ? 1.0f : 0.0f;
            check.Warn(SplitOutOfRange, "a left-right split outside 0 to 1 is clamped into it");
        }
        if (!(desc.torqueShare >= 0.0f))
        {
            desc.torqueShare = 0.0f;
            check.Warn(NegativeTorqueShare, "a negative torque share is treated as 0");
        }
        return desc;
    }

    Backend::VehicleAntiRollBarDesc AntiRollBarDescOf(std::uint32_t entity, const VehicleAntiRollBar& bar, int left,
                                                      int right, std::uint32_t& warned, ILogger& logger)
    {
        OutOfRange check("VehicleAntiRollBar", entity, warned, logger, OutOfRange::Lifetime::PerComponent);
        Backend::VehicleAntiRollBarDesc desc{.leftWheel = left, .rightWheel = right, .stiffness = bar.stiffness};
        // Written to catch NaN too.
        if (!(desc.stiffness >= 0.0f))
        {
            desc.stiffness = 0.0f;
            check.Warn(NegativeStiffness, "a negative stiffness is treated as 0");
        }
        return desc;
    }
} // namespace Fenrir::Detail
