// A physics world's vehicles (ADR-0015): each a backend vehicle on its chassis's body, removed with the joints before
// bodies go, made and given its driver input after the joints in PostTick step 2, run inside the step by the backend,
// and written back after the bodies in step 5, its wheel entities posed for drawing. Knows the ECS, not Jolt: it talks
// to Backend::World.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "Same.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        Entity ParentOf(const Entity& entity)
        {
            return entity.HasComponent<Relationship>() ? entity.GetComponent<Relationship>().parent : Entity();
        }

        // How many ancestors the entity has: wheels are posed parents before children.
        int DepthOf(const Entity& entity)
        {
            int depth = 0;
            for (Entity at = ParentOf(entity); at.IsValid(); at = ParentOf(at))
            {
                ++depth;
            }
            return depth;
        }

        bool Same(const JointSpring& a, const JointSpring& b)
        {
            return Fenrir::Same(a.frequency, b.frequency) && Fenrir::Same(a.damping, b.damping);
        }

        bool Same(const VehicleTyre& a, const VehicleTyre& b)
        {
            return Fenrir::Same(a.peakSlip, b.peakSlip) && Fenrir::Same(a.peakFriction, b.peakFriction) &&
                   Fenrir::Same(a.slideSlip, b.slideSlip) && Fenrir::Same(a.slideFriction, b.slideFriction);
        }

        // Whether two wheels have the same settings: every field but the vehicle it names and its state.
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
            return Fenrir::Same(a.forward, b.forward) && Fenrir::Same(a.steer, b.steer) &&
                   Fenrir::Same(a.brake, b.brake) && Fenrir::Same(a.handbrake, b.handbrake) &&
                   Fenrir::Same(a.leftTrack, b.leftTrack) && Fenrir::Same(a.rightTrack, b.rightTrack) &&
                   a.gear == b.gear && Fenrir::Same(a.clutch, b.clutch);
        }

        // Driver input in its ranges for the vehicle's `kind` (already in range), silently, as joint position targets
        // are. In Manual `forward` is a pedal, so reversing is the gear's job.
        VehicleInput Clamped(const VehicleInput& input, const VehicleKind& kind)
        {
            const VehicleTransmission transmission = Backend::DrivetrainOf(kind).transmission;
            const bool manual = transmission.mode == TransmissionMode::Manual;
            VehicleInput clamped = input;
            // A trailer has no engine or gearbox to drive.
            if (kind.Is<TrailerVehicle>())
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
            clamped.gear = std::clamp(clamped.gear, -1, transmission.gearCount);
            clamped.clutch = std::clamp(clamped.clutch, 0.0f, 1.0f);
            return clamped;
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

        // Whether changing from kind `a` to `b` rebuilds the vehicle: a different kind, or a motorcycle's lean limit.
        bool Rebuilds(const VehicleKind& a, const VehicleKind& b)
        {
            return a.Type() != b.Type() ||
                   (a.Is<MotorcycleVehicle>() &&
                    !Fenrir::Same(a.As<MotorcycleVehicle>().lean.maxAngle, b.As<MotorcycleVehicle>().lean.maxAngle));
        }

        bool Same(const VehicleTrack& a, const VehicleTrack& b)
        {
            return Fenrir::Same(a.inertia, b.inertia) && Fenrir::Same(a.angularDamping, b.angularDamping) &&
                   Fenrir::Same(a.maxBrakeTorque, b.maxBrakeTorque) &&
                   Fenrir::Same(a.differentialRatio, b.differentialRatio) &&
                   Fenrir::Same(a.longitudinalFriction, b.longitudinalFriction) &&
                   Fenrir::Same(a.lateralFriction, b.lateralFriction);
        }

        // Whether two kinds that do not rebuild (Rebuilds) have the same settings: a difference is a live edit.
        bool Same(const VehicleKind& a, const VehicleKind& b)
        {
            const WheeledVehicle x = Backend::DrivetrainOf(a);
            const WheeledVehicle y = Backend::DrivetrainOf(b);
            const bool sameLean =
                !a.Is<MotorcycleVehicle>() ||
                (a.As<MotorcycleVehicle>().lean.enabled == b.As<MotorcycleVehicle>().lean.enabled &&
                 a.As<MotorcycleVehicle>().lean.limitSteering == b.As<MotorcycleVehicle>().lean.limitSteering);
            const bool sameTracks =
                !a.Is<TrackedVehicle>() || (Same(a.As<TrackedVehicle>().left, b.As<TrackedVehicle>().left) &&
                                            Same(a.As<TrackedVehicle>().right, b.As<TrackedVehicle>().right));
            return Same(x.engine, y.engine) && Same(x.transmission, y.transmission) &&
                   Fenrir::Same(x.limitedSlipRatio, y.limitedSlipRatio) && sameLean && sameTracks;
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

        // Corrects a wheel's out-of-range settings into the ranges the backend can use, warning about each kind of
        // value the first time the wheel holds one.
        class WheelCorrector
        {
          public:
            WheelCorrector(std::uint32_t entity, std::uint32_t& warned, ILogger& logger)
                : m_entity(entity), m_warned(&warned), m_logger(&logger)
            {
            }

            [[nodiscard]] VehicleWheel Corrected(const VehicleWheel& wheel)
            {
                VehicleWheel corrected = wheel;
                NotNegative(corrected.width, NegativeSize, "a negative width is treated as 0");
                if (corrected.radius < minRadius)
                {
                    corrected.radius = minRadius;
                    Warn(SmallRadius, "a radius below 1 cm is raised to it");
                }
                NotNegative(corrected.inertia, NegativeWheelInertia, "a negative inertia is treated as 0");
                corrected.inertia = std::max(corrected.inertia, minInertia);
                NotNegative(corrected.angularDamping, NegativeDamping, "a negative angular damping is treated as 0");
                VehicleSuspension& suspension = corrected.suspension;
                for (float* value : {&suspension.travel, &suspension.preload, &suspension.spring.frequency,
                                     &suspension.spring.damping})
                {
                    NotNegative(*value, NegativeSuspension,
                                "a negative suspension travel, preload, frequency or damping is treated as 0");
                }
                for (float* value : {&corrected.maxBrakeTorque, &corrected.maxHandbrakeTorque})
                {
                    NotNegative(*value, NegativeBrakeTorque, "a negative brake torque is treated as 0");
                }
                for (float* angle : {&corrected.maxSteerAngle, &corrected.caster, &corrected.camber, &corrected.toe})
                {
                    if (*angle < -maxAngle || *angle > maxAngle)
                    {
                        *angle = std::clamp(*angle, -maxAngle, maxAngle);
                        Warn(AngleTooLarge,
                             "a maxSteerAngle, caster, camber or toe of a quarter turn or more is kept below it");
                    }
                }
                for (VehicleTyre* tyre : {&corrected.longitudinal, &corrected.lateral})
                {
                    NotNegative(tyre->peakFriction, NegativeFriction, "a negative tyre friction is treated as 0");
                    NotNegative(tyre->slideFriction, NegativeFriction, "a negative tyre friction is treated as 0");
                    NotNegative(tyre->peakSlip, BadSlip, "a negative peak slip is treated as 0");
                    if (tyre->slideSlip <= tyre->peakSlip)
                    {
                        tyre->slideSlip = tyre->peakSlip + 1e-4f;
                        Warn(BadSlip, "a slide slip not past the peak slip is moved just past it");
                    }
                }
                return corrected;
            }

          private:
            void Warn(WheelProblem problem, const char* message)
            {
                if ((*m_warned & problem) != 0)
                {
                    return;
                }
                *m_warned |= problem;
                m_logger->Warn("The VehicleWheel on entity " + std::to_string(m_entity) + ": " + message);
            }

            void NotNegative(float& value, WheelProblem problem, const char* message)
            {
                if (value < 0.0f)
                {
                    value = 0.0f;
                    Warn(problem, message);
                }
            }

            std::uint32_t m_entity;
            std::uint32_t* m_warned;
            ILogger* m_logger;
        };

        // A wheel for the backend, from its corrected settings and its position with the chassis's scale applied.
        // Caster tilts the steering axis's top back (+Z), camber the wheel's top out and toe its front in, mirrored
        // for a wheel on the left (x < 0).
        Backend::VehicleWheelDesc DescOf(const VehicleWheel& corrected, const Math::Vec3& position)
        {
            const float side = position.x < 0.0f ? -1.0f : 1.0f;
            Backend::VehicleWheelDesc desc;
            desc.position = position;
            desc.steeringAxis =
                Math::AngleAxis(corrected.caster, Math::Vec3(1.0f, 0.0f, 0.0f)) * Math::Vec3(0.0f, 1.0f, 0.0f);
            desc.rest = Math::AngleAxis(side * corrected.toe, Math::Vec3(0.0f, 1.0f, 0.0f)) *
                        Math::AngleAxis(-side * corrected.camber, Math::Vec3(0.0f, 0.0f, 1.0f));
            desc.settings = corrected;
            return desc;
        }

        // A tilt limit from 0 to a half turn, silently: a half turn or more is off.
        float TiltOf(const Vehicle& vehicle)
        {
            return std::clamp(vehicle.maxTilt, 0.0f, std::numbers::pi_v<float>);
        }

        // The engine's inertia an inertia of 0 is raised to: the backend divides by it.
        constexpr float minEngineInertia = 1e-3f;

        // The kinds of out-of-range value a vehicle's kind can hold, each warned about once per vehicle while it holds
        // one.
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

        constexpr std::pair<KindProblem, const char*> kindProblems[] = {
            {NegativeTorque, "a negative torque is treated as 0"},
            {NegativeInertia, "a negative engine inertia is treated as 0"},
            {EmptyRpmRange, "a maximum RPM at or below the minimum is raised above it"},
            {PeakTorqueOutOfRange, "a peak torque RPM outside the RPM range is moved into it"},
            {ShiftPointOutOfRange, "a shift point outside the RPM range is moved into it"},
            {ShiftDownNotBelowShiftUp, "a shift-down RPM at or above the shift-up RPM is lowered below it"},
            {NoGears, "a gear count below 1 is raised to 1"},
            {LimitedSlipAtMostOne, "a limited-slip ratio at or below 1 is raised to just above it"},
            {NegativeTrackValue, "a negative track inertia, brake torque or friction is treated as 0"},
        };

        // An engine and gearbox corrected into the ranges the backend can use, adding to `found` each kind of
        // out-of-range value. Values the spec does not name as out of range are corrected silently, just enough to keep
        // the backend's asserts happy.
        void Correct(VehicleEngine& engine, VehicleTransmission& transmission, std::uint32_t& found)
        {
            const auto notNegative = [&found](float& value, KindProblem problem) {
                if (value < 0.0f)
                {
                    value = 0.0f;
                    found |= problem;
                }
            };
            for (float* torque : {&engine.maxTorque, &engine.torqueAtMinRpm, &engine.torqueAtMaxRpm})
            {
                notNegative(*torque, NegativeTorque);
            }
            notNegative(engine.inertia, NegativeInertia);
            engine.inertia = std::max(engine.inertia, minEngineInertia);
            for (float* value : {&engine.minRpm, &engine.angularDamping, &transmission.switchTime,
                                 &transmission.clutchReleaseTime, &transmission.switchLatency})
            {
                *value = std::max(*value, 0.0f);
            }

            const auto outside = [&engine](float rpm) { return rpm < engine.minRpm || rpm > engine.maxRpm; };
            if (engine.maxRpm <= engine.minRpm)
            {
                found |= EmptyRpmRange;
            }
            engine.maxRpm = std::max(engine.maxRpm, engine.minRpm + 1.0f);
            if (outside(engine.peakTorqueRpm))
            {
                engine.peakTorqueRpm = std::clamp(engine.peakTorqueRpm, engine.minRpm, engine.maxRpm);
                found |= PeakTorqueOutOfRange;
            }

            if (transmission.gearCount < 1)
            {
                transmission.gearCount = 1;
                found |= NoGears;
            }
            for (float* ratio : {&transmission.firstGear, &transmission.topGear, &transmission.reverseGear})
            {
                *ratio = std::max(*ratio, 1e-3f);
            }
            // Shifting up below the top of the range and down above the bottom, with room between.
            if (outside(transmission.shiftUpRpm) || outside(transmission.shiftDownRpm))
            {
                found |= ShiftPointOutOfRange;
            }
            transmission.shiftUpRpm = std::clamp(transmission.shiftUpRpm, engine.minRpm + 0.5f, engine.maxRpm - 0.5f);
            transmission.shiftDownRpm = std::max(transmission.shiftDownRpm, std::max(engine.minRpm, 0.5f) - 0.25f);
            if (transmission.shiftDownRpm >= transmission.shiftUpRpm)
            {
                found |= ShiftDownNotBelowShiftUp;
            }
            transmission.shiftDownRpm = std::min(transmission.shiftDownRpm, transmission.shiftUpRpm - 0.25f);
            transmission.clutchStrength = std::max(transmission.clutchStrength, 1e-3f);
        }

        // The least inertia and differential ratio a track can have: the backend divides by both.
        constexpr float minTrackInertia = 1e-3f;
        constexpr float minTrackRatio = 1e-3f;

        // A track corrected into the ranges the backend can use, adding to `found` each kind of out-of-range value.
        void Correct(VehicleTrack& track, std::uint32_t& found)
        {
            for (float* value :
                 {&track.inertia, &track.maxBrakeTorque, &track.longitudinalFriction, &track.lateralFriction})
            {
                if (*value < 0.0f)
                {
                    *value = 0.0f;
                    found |= NegativeTrackValue;
                }
            }
            track.inertia = std::max(track.inertia, minTrackInertia);
            track.angularDamping = std::max(track.angularDamping, 0.0f);
            track.differentialRatio = std::max(track.differentialRatio, minTrackRatio);
        }

        // The kind corrected into the ranges the backend can use. The kinds of out-of-range value it held that
        // `warned` does not have are warned about, and `warned` becomes those it holds now, so a valid value clears
        // its warning.
        VehicleKind InRange(const VehicleKind& kind, std::uint32_t entity, std::uint32_t& warned, ILogger& logger)
        {
            VehicleKind corrected = kind;
            std::uint32_t found = 0;
            const auto correct = [&found](auto& drivetrain) {
                Correct(drivetrain.engine, drivetrain.transmission, found);
                if (drivetrain.limitedSlipRatio <= 1.0f)
                {
                    drivetrain.limitedSlipRatio = minLimitedSlipRatio;
                    found |= LimitedSlipAtMostOne;
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
                Correct(tracked.engine, tracked.transmission, found);
                Correct(tracked.left, found);
                Correct(tracked.right, found);
            }
            else if (corrected.Is<WheeledVehicle>())
            {
                correct(corrected.As<WheeledVehicle>());
            }
            for (const auto& [problem, message] : kindProblems)
            {
                if ((found & problem) != 0 && (warned & problem) == 0)
                {
                    logger.Warn("The Vehicle on entity " + std::to_string(entity) + ": " + message);
                }
            }
            warned = found;
            return corrected;
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
        bool Same(const std::vector<T>& a, const std::vector<T>& b)
        {
            return std::ranges::equal(a, b, [](const T& x, const T& y) { return Same(x, y); });
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

    } // namespace

    Transform PhysicsWorld::Impl::WheelPose(const Backend::VehicleWheelDesc& wheel,
                                            const Backend::VehicleWheelRead& read)
    {
        // Rolling forward turns the wheel's top towards -Z.
        const Math::Quat steer = Math::AngleAxis(-read.steerAngle, wheel.steeringAxis);
        const Math::Quat spin = Math::AngleAxis(-read.spinAngle, Math::Vec3(1.0f, 0.0f, 0.0f));
        Transform pose;
        pose.pos = wheel.position + wheel.steeringAxis * read.compression;
        pose.rot = steer * wheel.rest * spin;
        return pose;
    }

    void PhysicsWorld::Impl::DrainVehicles()
    {
        // A Vehicle removed, or removed and added again, loses its backend vehicle: it is made again from what the
        // entity has now.
        for (const std::uint32_t entity : m_vehicleEntities.Drain())
        {
            if (const auto it = m_vehicles.find(entity); it != m_vehicles.end())
            {
                DropVehicle(it->second, false);
                m_vehicles.erase(it);
            }
        }
        // A wheel that came or went, even one that came back, changes its vehicle's wheel set (a rebuild, carrying
        // over), and is warned about afresh.
        for (const std::uint32_t entity : m_wheelEntities.Drain())
        {
            m_wheelWarnings.erase(entity);
            for (auto& [chassis, record] : m_vehicles)
            {
                if (std::ranges::find(record.wheels, entity) != record.wheels.end())
                {
                    DropVehicle(record, true);
                }
            }
        }
        // A differential or anti-roll bar removed, or removed and added again, is new: it is warned about afresh. Its
        // edits and relinks are diffed in step 2.
        const auto link = [](TrackedComponents& tracked, std::map<std::uint32_t, VehicleLinkRecord>& records) {
            for (const std::uint32_t entity : tracked.Drain())
            {
                records.erase(entity);
                if (tracked.Contains(entity))
                {
                    records.try_emplace(entity);
                }
            }
        };
        link(m_differentialEntities, m_differentials);
        link(m_antiRollBarEntities, m_antiRollBars);
    }

    std::map<std::uint32_t, PhysicsWorld::Impl::VehicleLinks> PhysicsWorld::Impl::LinksByVehicle(
        const std::map<std::uint32_t, std::vector<std::uint32_t>>& wheelsOf)
    {
        // The vehicle a differential or anti-roll bar's wheels belong to, and their wheel indices, -1 for none. A name
        // that has died (its entity destroyed, or its VehicleWheel removed while named) is none, warned about once.
        // None for one that names no live wheel of a vehicle's wheel set, or is inactive: then it warns, if it was not
        // already.
        struct Named
        {
            std::uint32_t chassis;
            int left;
            int right;
        };
        const auto name = [this, &wheelsOf](const char* type, std::uint32_t entity, VehicleLinkRecord& record,
                                            const Entity& left, const Entity& right) -> std::optional<Named> {
            std::string wrong;
            std::optional<Entity> vehicle;
            std::set<std::uint32_t> alive;
            std::set<std::uint32_t> dead;
            for (const Entity& wheel : {left, right})
            {
                if (wheel == Entity())
                {
                    continue;
                }
                const std::uint32_t id = wheel.GetId();
                if (!wheel.IsValid())
                {
                    dead.insert(id);
                    continue;
                }
                if (wheel != m_entities->HandleOf(id) || !wheel.HasComponent<VehicleWheel>())
                {
                    if (record.wheels.contains(id))
                    {
                        alive.insert(id);
                        dead.insert(id);
                    }
                    else if (wrong.empty())
                    {
                        wrong = "it names entity " + std::to_string(id) + ", which is not a wheel entity";
                    }
                    continue;
                }
                alive.insert(id);
                const Entity& chassis = wheel.GetComponent<VehicleWheel>().vehicle;
                if (vehicle && chassis != *vehicle && wrong.empty())
                {
                    wrong = "its wheels belong to two different vehicles";
                }
                vehicle = chassis;
            }
            for (const std::uint32_t id : dead)
            {
                if (!record.dead.contains(id))
                {
                    m_logger->Warn(std::string("The ") + type + " on entity " + std::to_string(entity) +
                                   " names entity " + std::to_string(id) +
                                   ", whose wheel has died (the entity destroyed or its VehicleWheel removed): it "
                                   "treats that name as empty.");
                }
            }
            record.wheels = std::move(alive);
            record.dead = std::move(dead);
            if (!wrong.empty())
            {
                if (!record.inactive)
                {
                    m_logger->Warn(std::string("The ") + type + " on entity " + std::to_string(entity) +
                                   " is inactive: " + wrong +
                                   ". It does nothing until its wheels are wheel entities of one vehicle.");
                    record.inactive = true;
                }
                return std::nullopt;
            }
            record.inactive = false;
            // Naming no live wheel, or a wheel that is not one of its vehicle's wheels now: nothing to do. With no
            // live wheel it names no vehicle, so one that was its vehicle's only differential leaves it driving every
            // wheel.
            const auto wheels = vehicle && vehicle->IsValid() ? wheelsOf.find(vehicle->GetId()) : wheelsOf.end();
            if (wheels == wheelsOf.end())
            {
                return std::nullopt;
            }
            constexpr int none = -1;
            constexpr int notOfTheSet = -2;
            const auto indexOf = [&wheels, &record](const Entity& wheel) {
                if (wheel == Entity() || record.dead.contains(wheel.GetId()))
                {
                    return none;
                }
                const auto at = std::ranges::find(wheels->second, wheel.GetId());
                return at == wheels->second.end() ? notOfTheSet : static_cast<int>(at - wheels->second.begin());
            };
            const Named named{.chassis = vehicle->GetId(), .left = indexOf(left), .right = indexOf(right)};
            if (named.left == notOfTheSet || named.right == notOfTheSet)
            {
                return std::nullopt;
            }
            return named;
        };
        const auto warn = [this](const char* type, std::uint32_t entity, VehicleLinkRecord& record, LinkProblem problem,
                                 const char* message) {
            if ((record.warned & problem) == 0)
            {
                record.warned |= problem;
                m_logger->Warn(std::string("The ") + type + " on entity " + std::to_string(entity) + ": " + message);
            }
        };

        std::map<std::uint32_t, VehicleLinks> links;
        // In entity order, as the records are.
        for (auto& [entity, record] : m_differentials)
        {
            const auto& differential = m_entities->GetEntity(entity).GetComponent<VehicleDifferential>();
            const char* type = "VehicleDifferential";
            const std::optional<Named> named =
                name(type, entity, record, differential.leftWheel, differential.rightWheel);
            if (!named)
            {
                continue;
            }
            Backend::VehicleDifferentialDesc desc{.leftWheel = named->left,
                                                  .rightWheel = named->right,
                                                  .ratio = differential.ratio,
                                                  .leftRightSplit = differential.leftRightSplit,
                                                  .limitedSlipRatio = differential.limitedSlipRatio,
                                                  .torqueShare = differential.torqueShare};
            // Written to catch NaN too.
            if (!(desc.limitedSlipRatio >= minLimitedSlipRatio))
            {
                desc.limitedSlipRatio = minLimitedSlipRatio;
                warn(type, entity, record, LimitedSlipAtOrBelowOne,
                     "a limited-slip ratio at or below 1 is raised to just above it");
            }
            if (!(desc.ratio >= minDifferentialRatio))
            {
                desc.ratio = minDifferentialRatio;
                warn(type, entity, record, RatioNotPositive, "a ratio at or below 0 is raised to a small minimum");
            }
            if (!(desc.leftRightSplit >= 0.0f && desc.leftRightSplit <= 1.0f))
            {
                desc.leftRightSplit = desc.leftRightSplit > 1.0f ? 1.0f : 0.0f;
                warn(type, entity, record, SplitOutOfRange, "a left-right split outside 0 to 1 is clamped into it");
            }
            if (!(desc.torqueShare >= 0.0f))
            {
                desc.torqueShare = 0.0f;
                warn(type, entity, record, NegativeTorqueShare, "a negative torque share is treated as 0");
            }
            links[named->chassis].differentials.push_back(desc);
        }
        for (auto& [entity, record] : m_antiRollBars)
        {
            const auto& bar = m_entities->GetEntity(entity).GetComponent<VehicleAntiRollBar>();
            const char* type = "VehicleAntiRollBar";
            const std::optional<Named> named = name(type, entity, record, bar.leftWheel, bar.rightWheel);
            // With a wheel empty, it has nothing to push against.
            if (!named || named->left < 0 || named->right < 0)
            {
                continue;
            }
            Backend::VehicleAntiRollBarDesc desc{
                .leftWheel = named->left, .rightWheel = named->right, .stiffness = bar.stiffness};
            if (!(desc.stiffness >= 0.0f))
            {
                desc.stiffness = 0.0f;
                warn(type, entity, record, NegativeStiffness, "a negative stiffness is treated as 0");
            }
            links[named->chassis].antiRollBars.push_back(desc);
        }
        return links;
    }

    void PhysicsWorld::Impl::RemoveVehiclesOf(std::span<const Backend::BodyId> bodies)
    {
        for (auto& [chassis, record] : m_vehicles)
        {
            if (record.Made() && std::ranges::any_of(bodies, [&record](const Backend::BodyId body) {
                    return body.value == record.body.value;
                }))
            {
                // Rebuilt in this drain, it carries over; gone for good, it is inactive in step 2, which forgets it.
                DropVehicle(record, true);
            }
        }
    }

    void PhysicsWorld::Impl::DropVehicle(VehicleRecord& record, bool carryOver)
    {
        std::optional<VehicleRecord::Carry> carry = carryOver ? record.carry : std::nullopt;
        if (record.Made())
        {
            if (carryOver)
            {
                const Backend::VehicleRead read = m_backend.ReadVehicle(record.id);
                carry = VehicleRecord::Carry{.engineRpm = read.state.engineRpm,
                                             .gear = read.state.gear,
                                             .clutch = read.state.clutch,
                                             .wheels = {}};
                for (std::size_t i = 0; i < record.wheels.size(); ++i)
                {
                    carry->wheels[record.wheels[i]] = Backend::VehicleWheelSpin{
                        .angularVelocity = read.wheels[i].angularVelocity, .spinAngle = read.wheels[i].spinAngle};
                }
            }
            m_backend.DestroyVehicle(record.id);
        }
        // What was warned about stays warned about: a rebuild is no reason to warn again.
        const std::uint32_t warned = record.warned;
        record = VehicleRecord{};
        record.warned = warned;
        record.carry = std::move(carry);
    }

    std::string PhysicsWorld::Impl::WhyNoChassis(std::uint32_t chassis) const
    {
        const Entity entity = m_entities->GetEntity(chassis);
        if (entity.HasComponent<CharacterBody>())
        {
            return "it is beside a CharacterBody";
        }
        if (!entity.HasComponent<RigidBody>())
        {
            return "it has no RigidBody";
        }
        const auto it = m_bodies.find(chassis);
        if (it == m_bodies.end() || !it->second.Active())
        {
            return "its RigidBody has no collider physics could make";
        }
        if (it->second.isTrigger)
        {
            return "its collider is a trigger, which gives its RigidBody no solid collider";
        }
        if (it->second.motion.motionType != MotionType::Dynamic)
        {
            return it->second.motion.motionType == MotionType::Kinematic ? "its RigidBody is kinematic"
                                                                         : "its RigidBody is static";
        }
        return {};
    }

    const PhysicsWorld::Impl::Record* PhysicsWorld::Impl::ChassisBodyOf(std::uint32_t chassis) const
    {
        return WhyNoChassis(chassis).empty() ? &m_bodies.at(chassis) : nullptr;
    }

    std::string PhysicsWorld::Impl::WhyNoWheel(std::uint32_t wheel) const
    {
        const Entity entity = m_entities->GetEntity(wheel);
        const Entity& chassis = entity.GetComponent<VehicleWheel>().vehicle;
        // A wheel names a vehicle of its own scene, and is no body itself.
        if (!chassis.IsValid())
        {
            return "its vehicle is empty or not alive";
        }
        const std::string named = "its vehicle (entity " + std::to_string(chassis.GetId()) + ")";
        if (chassis != m_entities->HandleOf(chassis.GetId()))
        {
            return named + " is in another scene";
        }
        if (chassis.GetId() == wheel)
        {
            return "it is on its vehicle's chassis entity";
        }
        if (!m_vehicleEntities.Contains(chassis.GetId()))
        {
            return named + " has no Vehicle";
        }
        for (const auto& [has, component] : {std::pair{entity.HasComponent<RigidBody>(), "a RigidBody"},
                                             std::pair{entity.HasComponent<CharacterBody>(), "a CharacterBody"},
                                             std::pair{entity.HasComponent<Collider>(), "a Collider"}})
        {
            if (has)
            {
                return std::string("it shares its entity with ") + component;
            }
        }
        return {};
    }

    std::map<std::uint32_t, std::vector<std::uint32_t>> PhysicsWorld::Impl::WheelsByVehicle() const
    {
        std::map<std::uint32_t, std::vector<std::uint32_t>> wheels;
        for (const std::uint32_t id : m_wheelEntities.Entities())
        {
            if (WhyNoWheel(id).empty())
            {
                // In entity order, as m_wheelEntities is.
                wheels[m_entities->GetEntity(id).GetComponent<VehicleWheel>().vehicle.GetId()].push_back(id);
            }
        }
        return wheels;
    }

    void PhysicsWorld::Impl::WarnAboutInactiveVehicles()
    {
        const auto warn = [this](const std::set<std::uint32_t>& entities, std::set<std::uint32_t>& inactive, auto why,
                                 const std::string& component, const std::string& until) {
            std::erase_if(inactive, [&entities](std::uint32_t id) { return !entities.contains(id); });
            for (const std::uint32_t id : entities)
            {
                const std::string reason = why(id);
                if (reason.empty())
                {
                    inactive.erase(id);
                }
                else if (inactive.insert(id).second)
                {
                    m_logger->Warn("The " + component + " on entity " + std::to_string(id) + " is inactive: " + reason +
                                   ". " + until);
                }
            }
        };
        warn(
            m_vehicleEntities.Entities(), m_inactiveVehicles, [this](std::uint32_t id) { return WhyNoChassis(id); },
            "Vehicle",
            "It is built fresh once its entity has a dynamic RigidBody with a solid collider and no CharacterBody.");
        warn(
            m_wheelEntities.Entities(), m_inactiveWheels, [this](std::uint32_t id) { return WhyNoWheel(id); },
            "VehicleWheel",
            "It is left out of its vehicle until it names another entity of its scene with a Vehicle and shares its "
            "entity with no RigidBody, CharacterBody or Collider.");
    }

    void PhysicsWorld::Impl::SyncVehicles()
    {
        WarnAboutInactiveVehicles();
        std::map<std::uint32_t, std::vector<std::uint32_t>> wheelsOf = WheelsByVehicle();
        std::map<std::uint32_t, VehicleLinks> linksOf = LinksByVehicle(wheelsOf);
        // In chassis entity order, so vehicles are made, and so solved, in a reproducible order.
        for (const std::uint32_t chassis : m_vehicleEntities.Entities())
        {
            VehicleRecord& record = m_vehicles[chassis];
            Entity entity = m_entities->GetEntity(chassis);
            const Vehicle& vehicle = entity.GetComponent<Vehicle>();
            const Record* body = ChassisBodyOf(chassis);
            const std::vector<std::uint32_t>& wheels = wheelsOf[chassis];
            if (!vehicle.enabled || body == nullptr || wheels.empty())
            {
                // Disabled, inactive, or with nothing to drive it: a plain rigid body, if any, its state reset in step
                // 5. Enabled and valid again, it is built fresh: the record holds nothing to carry over.
                DropVehicle(record, false);
                continue;
            }

            const VehicleLinks& links = linksOf[chassis];
            bool wake = false;
            if (!record.Made() || record.body.value != body->id.value || record.wheels != wheels ||
                Rebuilds(record.kind, vehicle.kind))
            {
                // A wheel-set change, the chassis's body rebuilt, a kind change or a motorcycle's lean limit: what
                // still maps carries over, a new wheel still.
                DropVehicle(record, true);
                Backend::VehicleDesc desc;
                desc.body = body->id;
                desc.kind = InRange(vehicle.kind, chassis, record.warned, *m_logger);
                desc.probe = vehicle.probe;
                desc.maxTilt = TiltOf(vehicle);
                desc.priority = chassis;
                desc.differentials = links.differentials;
                desc.antiRollBars = links.antiRollBars;
                for (const std::uint32_t wheel : wheels)
                {
                    const VehicleWheel& settings = m_entities->GetEntity(wheel).GetComponent<VehicleWheel>();
                    desc.wheels.push_back(WheelDesc(wheel, settings, body->world.scale));
                    record.appliedWheels.push_back(settings);
                }
                if (record.carry)
                {
                    desc.carried = Backend::VehicleCarry{
                        .engineRpm = record.carry->engineRpm,
                        .gear = std::min(record.carry->gear, Backend::DrivetrainOf(desc.kind).transmission.gearCount),
                        .clutch = record.carry->clutch,
                        .wheels = {}};
                    for (const std::uint32_t wheel : wheels)
                    {
                        const auto spin = record.carry->wheels.find(wheel);
                        desc.carried->wheels.push_back(spin == record.carry->wheels.end() ? Backend::VehicleWheelSpin{}
                                                                                          : spin->second);
                    }
                    record.carry.reset();
                }
                record.id = m_backend.CreateVehicle(desc);
                record.body = body->id;
                record.wheels = wheels;
                record.links = links;
                record.given = desc.wheels;
                record.probe = vehicle.probe;
                record.maxTilt = vehicle.maxTilt;
                record.kind = vehicle.kind;
                record.applied = desc.kind;
                PoseWheels(record, m_backend.ReadVehicle(record.id), body->world);
                wake = true;
            }
            else
            {
                if (!Same(vehicle.kind, record.kind))
                {
                    // A live edit: the engine, the gearbox, the limited-slip ratio, a motorcycle's lean switches and a
                    // tracked vehicle's tracks, from the next step.
                    record.applied = InRange(vehicle.kind, chassis, record.warned, *m_logger);
                    m_backend.UpdateVehicle(record.id, record.applied);
                    record.kind = vehicle.kind;
                    wake = true;
                }
                // The wheels, the probe and the tilt limit.
                wake = SyncVehicleSettings(record, vehicle, body->world.scale) || wake;
            }
            // Differential and anti-roll-bar edits and relinks, live.
            if (!Same(links.differentials, record.links.differentials))
            {
                m_backend.SetVehicleDifferentials(record.id, links.differentials);
                record.links.differentials = links.differentials;
                wake = true;
            }
            if (!Same(links.antiRollBars, record.links.antiRollBars))
            {
                m_backend.SetVehicleAntiRollBars(record.id, links.antiRollBars);
                record.links.antiRollBars = links.antiRollBars;
                wake = true;
            }

            // Any change wakes it, even one its ranges clamp away.
            wake = wake || !Same(vehicle.input, record.input);
            m_backend.SetVehicleInput(record.id, Clamped(vehicle.input, record.applied), wake);
            record.input = vehicle.input;
        }
    }

    Backend::VehicleWheelDesc PhysicsWorld::Impl::WheelDesc(std::uint32_t entity, const VehicleWheel& wheel,
                                                            const Math::Vec3& scale)
    {
        const VehicleWheel corrected = WheelCorrector(entity, m_wheelWarnings[entity], *m_logger).Corrected(wheel);
        return DescOf(corrected, wheel.position * scale);
    }

    bool PhysicsWorld::Impl::SyncVehicleSettings(VehicleRecord& record, const Vehicle& vehicle, const Math::Vec3& scale)
    {
        bool changed = false;
        for (std::size_t i = 0; i < record.wheels.size(); ++i)
        {
            const VehicleWheel& wheel = m_entities->GetEntity(record.wheels[i]).GetComponent<VehicleWheel>();
            // A wheel's position follows the chassis's scale, so a scale change moves it, as an edit does.
            if (Same(wheel, record.appliedWheels[i]) && Fenrir::Same(wheel.position * scale, record.given[i].position))
            {
                continue;
            }
            record.given[i] = WheelDesc(record.wheels[i], wheel, scale);
            m_backend.UpdateVehicleWheel(record.id, i, record.given[i]);
            record.appliedWheels[i] = wheel;
            changed = true;
        }
        if (vehicle.probe != record.probe)
        {
            m_backend.SetVehicleProbe(record.id, vehicle.probe);
            record.probe = vehicle.probe;
            changed = true;
        }
        if (!Fenrir::Same(vehicle.maxTilt, record.maxTilt))
        {
            m_backend.SetVehicleMaxTilt(record.id, TiltOf(vehicle));
            record.maxTilt = vehicle.maxTilt;
            changed = true;
        }
        return changed;
    }

    void PhysicsWorld::Impl::PoseWheels(const VehicleRecord& record, const Backend::VehicleRead& read,
                                        const Transform& chassis)
    {
        // Parents before children: a wheel's local Transform is written against its parent's world transform.
        std::vector<std::tuple<int, std::uint32_t, std::size_t>> order;
        for (std::size_t i = 0; i < record.wheels.size(); ++i)
        {
            order.emplace_back(DepthOf(m_entities->GetEntity(record.wheels[i])), record.wheels[i], i);
        }
        std::ranges::sort(order);
        for (const auto& [depth, id, i] : order)
        {
            Entity wheel = m_entities->GetEntity(id);
            const Transform local = WheelPose(record.given[i], read.wheels[i]);
            // The scale stays game code's.
            Transform world = GetWorldTransform(wheel);
            world.pos = chassis.pos + chassis.rot * local.pos;
            world.rot = chassis.rot * local.rot;
            SetWorldTransform(wheel, world);
        }
    }

    void PhysicsWorld::Impl::WriteBackVehicles(float dt)
    {
        const auto handle = [this](std::uint32_t id) { return id == ~0u ? Entity() : m_entities->HandleOf(id); };
        std::set<std::uint32_t> posed;
        for (auto& [chassis, record] : m_vehicles)
        {
            Vehicle& vehicle = m_entities->GetEntity(chassis).GetComponent<Vehicle>();
            if (!record.Made())
            {
                vehicle.state = VehicleState{};
                continue;
            }
            // Asleep, it keeps what the step it fell asleep left: the backend stopped its wheels then.
            if (!m_backend.IsSleeping(record.body))
            {
                record.parked.reset();
            }
            else if (!record.parked)
            {
                record.parked = m_backend.ReadVehicle(record.id);
            }
            const Backend::VehicleRead read = record.parked ? *record.parked : m_backend.ReadVehicle(record.id);
            vehicle.state = read.state;
            for (std::size_t i = 0; i < record.wheels.size(); ++i)
            {
                const Backend::VehicleWheelRead& wheel = read.wheels[i];
                m_entities->GetEntity(record.wheels[i]).GetComponent<VehicleWheel>().state =
                    VehicleWheelState{.onGround = wheel.onGround,
                                      .groundBody = handle(wheel.groundEntity),
                                      .groundCollider = handle(wheel.groundCollider),
                                      .contactPoint = wheel.contactPoint,
                                      .contactNormal = wheel.contactNormal,
                                      .groundVelocity = wheel.groundVelocity,
                                      .compression = wheel.compression,
                                      .suspensionForce = wheel.suspensionForce,
                                      .longitudinalSlip = wheel.longitudinalSlip,
                                      .lateralSlip = wheel.lateralSlip,
                                      .angularVelocity = wheel.angularVelocity,
                                      .steerAngle = wheel.steerAngle};
                posed.insert(record.wheels[i]);
            }
            // After the chassis's write-back, against its step pose.
            const Record& body = m_bodies.at(chassis);
            PoseWheels(record, read, body.world);
            AdvanceWheels(record, read, dt, body.shown.snap);
        }
        // A wheel of no made vehicle: its state reset, and its Transform left where it was.
        for (const std::uint32_t wheel : m_wheelEntities.Entities())
        {
            if (!posed.contains(wheel))
            {
                m_entities->GetEntity(wheel).GetComponent<VehicleWheel>().state = VehicleWheelState{};
            }
        }
    }

    void PhysicsWorld::Impl::AdvanceWheels(VehicleRecord& record, const Backend::VehicleRead& read, float dt,
                                           bool teleported)
    {
        const bool snap = record.snap || teleported || record.current.size() != read.wheels.size();
        constexpr float turn = 2.0f * std::numbers::pi_v<float>;
        std::vector<VehicleRecord::WheelShown> now;
        for (std::size_t i = 0; i < read.wheels.size(); ++i)
        {
            const Backend::VehicleWheelRead& wheel = read.wheels[i];
            VehicleRecord::WheelShown shown{
                .compression = wheel.compression, .steer = wheel.steerAngle, .spin = wheel.spinAngle};
            if (!snap)
            {
                // The backend wraps the angle: unwrapped, it is the one nearest where the wheel's speed took it.
                const float expected = record.current[i].spin + wheel.angularVelocity * dt;
                shown.spin += turn * std::round((expected - shown.spin) / turn);
            }
            now.push_back(shown);
        }
        record.previous = snap ? now : std::move(record.current);
        record.current = std::move(now);
        // Both by the same whole turns, so the angles stay small.
        for (std::size_t i = 0; i < record.current.size(); ++i)
        {
            const float turns = turn * std::round(record.previous[i].spin / turn);
            record.previous[i].spin -= turns;
            record.current[i].spin -= turns;
        }
        record.snap = false;
    }

    void PhysicsWorld::Impl::BlendWheels(float fraction)
    {
        for (auto& [chassis, record] : m_vehicles)
        {
            // Shown as its chassis is: only a chassis Blend blended (interpolated, and not held by game code) carries
            // its wheels between steps. Otherwise they stay at their step poses.
            const auto body = m_bodies.find(chassis);
            if (!record.Made() || record.current.size() != record.wheels.size() || body == m_bodies.end() ||
                !body->second.shown.blended || !m_entities->HasEntity(chassis))
            {
                continue;
            }
            const Entity entity = m_entities->GetEntity(chassis);
            if (!entity.HasComponent<RigidBody>() || !entity.GetComponent<RigidBody>().interpolate)
            {
                continue;
            }
            // A wheel entity destroyed since the last step, in a frame with no tick: its vehicle is rebuilt in the next
            // drain, and until then its wheels stay at their step poses.
            if (!std::ranges::all_of(record.wheels, [this](std::uint32_t id) { return m_entities->HasEntity(id); }))
            {
                continue;
            }
            const Transform at = body->second.shown.Between(fraction);
            // Every true local Transform is saved before any is overwritten.
            record.trueLocals.clear();
            for (const std::uint32_t id : record.wheels)
            {
                const Entity wheel = m_entities->GetEntity(id);
                record.trueLocals.emplace_back(wheel, wheel.GetComponent<Transform>());
            }
            // Parents before children, as PoseWheels does.
            std::vector<std::tuple<int, std::uint32_t, std::size_t>> order;
            for (std::size_t i = 0; i < record.wheels.size(); ++i)
            {
                order.emplace_back(DepthOf(m_entities->GetEntity(record.wheels[i])), record.wheels[i], i);
            }
            std::ranges::sort(order);
            for (const auto& [depth, id, i] : order)
            {
                const VehicleRecord::WheelShown& from = record.previous[i];
                const VehicleRecord::WheelShown& to = record.current[i];
                Backend::VehicleWheelRead between;
                between.compression = from.compression + (to.compression - from.compression) * fraction;
                between.steerAngle = from.steer + (to.steer - from.steer) * fraction;
                between.spinAngle = from.spin + (to.spin - from.spin) * fraction;
                const Transform local = WheelPose(record.given[i], between);
                Entity wheel = m_entities->GetEntity(id);
                // The scale stays game code's.
                Transform world = GetWorldTransform(wheel);
                world.pos = at.pos + at.rot * local.pos;
                world.rot = at.rot * local.rot;
                SetWorldTransform(wheel, world);
            }
            record.blended = true;
        }
    }

    void PhysicsWorld::Impl::RestoreWheels()
    {
        // A wheel's Transform is output only: whatever game code wrote since goes, as the next step would overwrite it.
        for (auto& [chassis, record] : m_vehicles)
        {
            if (!record.blended)
            {
                continue;
            }
            // A wheel entity destroyed since is skipped, though its id may name a newer entity.
            for (const auto& [wheel, local] : record.trueLocals)
            {
                if (wheel.IsValid())
                {
                    wheel.GetComponent<Transform>() = local;
                }
            }
            record.blended = false;
        }
    }
} // namespace Fenrir
