// A physics world's vehicles (ADR-0015): each a backend vehicle on its chassis's body, removed with the joints before
// bodies go, made and given its driver input after the joints in PostTick's edits, run inside the step by the backend,
// and written back with the bodies in its write-back, its wheel entities' poses handed to Poses for drawing and blended
// between ticks, and its debug lines. Knows the ECS, not Jolt: it talks to Backend::World.

#include "Vehicles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "DebugCanvas.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "Poses.hpp"
#include "Same.hpp"
#include "VehicleSettings.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Half the size of the cross-tick at a vehicle wheel's centre on its suspension.
        constexpr float suspensionTick = 0.08f;

        // A wheel's contact colour: the worse of its two slips against its tyres'.
        std::uint32_t GripColour(const Backend::VehicleWheelDesc& wheel, const Backend::VehicleWheelRead& read)
        {
            const float along = std::abs(read.longitudinalSlip);
            const float across = std::abs(read.lateralSlip);
            if (along > wheel.longitudinal.slideSlip || across > wheel.lateral.slideSlip)
            {
                return DebugColour::PastSlideSlip;
            }
            if (along > wheel.longitudinal.peakSlip || across > wheel.lateral.peakSlip)
            {
                return DebugColour::PastPeakSlip;
            }
            return DebugColour::Gripping;
        }
    } // namespace

    Vehicles::Vehicles(EntityList& entities, ILogger& logger, Backend::World& backend, const Bodies& bodies,
                       Poses& poses)
        : m_entities(&entities), m_logger(&logger), m_backend(&backend), m_bodies(&bodies), m_poses(&poses),
          m_vehicleEntities(entities, ComponentType::Native<Vehicle>()),
          m_wheelEntities(entities, ComponentType::Native<VehicleWheel>()),
          m_inactiveVehicles(logger,
                             [](std::uint32_t entity, const std::string& why) {
                                 return "The Vehicle on entity " + std::to_string(entity) + " is inactive: " + why +
                                        ". It is built fresh once its entity has a dynamic RigidBody with a solid "
                                        "collider and no CharacterBody.";
                             }),
          m_inactiveWheels(logger,
                           [](std::uint32_t entity, const std::string& why) {
                               return "The VehicleWheel on entity " + std::to_string(entity) + " is inactive: " + why +
                                      ". It is left out of its vehicle until it names another entity of its scene "
                                      "with a Vehicle and shares its entity with no RigidBody, CharacterBody or "
                                      "Collider.";
                           }),
          m_differentialEntities(entities, ComponentType::Native<VehicleDifferential>()),
          m_antiRollBarEntities(entities, ComponentType::Native<VehicleAntiRollBar>()),
          m_inactiveDifferentials(logger,
                                  [](std::uint32_t entity, const std::string& why) {
                                      return "The VehicleDifferential on entity " + std::to_string(entity) +
                                             " is inactive: " + why +
                                             ". It does nothing until its wheels are wheel entities of one vehicle.";
                                  }),
          m_inactiveAntiRollBars(logger, [](std::uint32_t entity, const std::string& why) {
              return "The VehicleAntiRollBar on entity " + std::to_string(entity) + " is inactive: " + why +
                     ". It does nothing until its wheels are wheel entities of one vehicle.";
          })
    {
        for (const std::uint32_t entity : m_differentialEntities.Entities())
        {
            m_differentials.try_emplace(entity);
        }
        for (const std::uint32_t entity : m_antiRollBarEntities.Entities())
        {
            m_antiRollBars.try_emplace(entity);
        }
    }

    Transform Vehicles::WheelPose(const Backend::VehicleWheelDesc& wheel, const Backend::VehicleWheelRead& read)
    {
        // Rolling forward turns the wheel's top towards -Z.
        const Math::Quat steer = Math::AngleAxis(-read.steerAngle, wheel.steeringAxis);
        const Math::Quat spin = Math::AngleAxis(-read.spinAngle, Math::Vec3(1.0f, 0.0f, 0.0f));
        Transform pose;
        pose.pos = wheel.position + wheel.steeringAxis * read.compression;
        pose.rot = steer * wheel.rest * spin;
        return pose;
    }

    void Vehicles::Drain()
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
        // edits and relinks are diffed in the edits.
        const auto link = [](TrackedComponents& tracked, std::map<std::uint32_t, VehicleLinkRecord>& records,
                             InactiveTracker& inactive) {
            for (const std::uint32_t entity : tracked.Drain())
            {
                records.erase(entity);
                inactive.Forget(entity);
                if (tracked.Contains(entity))
                {
                    records.try_emplace(entity);
                }
            }
        };
        link(m_differentialEntities, m_differentials, m_inactiveDifferentials);
        link(m_antiRollBarEntities, m_antiRollBars, m_inactiveAntiRollBars);
    }

    std::map<std::uint32_t, Vehicles::VehicleLinks> Vehicles::LinksByVehicle(
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
                                            InactiveTracker& inactive, const Entity& left,
                                            const Entity& right) -> std::optional<Named> {
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
            if (inactive.Report(entity, wrong))
            {
                return std::nullopt;
            }
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
        std::map<std::uint32_t, VehicleLinks> links;
        // In entity order, as the records are.
        for (auto& [entity, record] : m_differentials)
        {
            const auto& differential = m_entities->GetEntity(entity).GetComponent<VehicleDifferential>();
            const std::optional<Named> named = name("VehicleDifferential", entity, record, m_inactiveDifferentials,
                                                    differential.leftWheel, differential.rightWheel);
            if (!named)
            {
                continue;
            }
            links[named->chassis].differentials.push_back(
                DifferentialDescOf(entity, differential, named->left, named->right, record.warned, *m_logger));
        }
        for (auto& [entity, record] : m_antiRollBars)
        {
            const auto& bar = m_entities->GetEntity(entity).GetComponent<VehicleAntiRollBar>();
            const std::optional<Named> named =
                name("VehicleAntiRollBar", entity, record, m_inactiveAntiRollBars, bar.leftWheel, bar.rightWheel);
            // With a wheel empty, it has nothing to push against.
            if (!named || named->left < 0 || named->right < 0)
            {
                continue;
            }
            links[named->chassis].antiRollBars.push_back(
                AntiRollBarDescOf(entity, bar, named->left, named->right, record.warned, *m_logger));
        }
        return links;
    }

    void Vehicles::DropOn(std::span<const Backend::BodyId> bodies)
    {
        for (auto& [chassis, record] : m_vehicles)
        {
            if (record.Made() && std::ranges::any_of(bodies, [&record](const Backend::BodyId body) {
                    return body.value == record.desc.body.value;
                }))
            {
                // Rebuilt in this drain, it carries over; gone for good, it is inactive in the edits, which forget it.
                DropVehicle(record, true);
            }
        }
    }

    void Vehicles::DropVehicle(VehicleRecord& record, bool carryOver)
    {
        std::optional<VehicleRecord::Carry> carry = carryOver ? record.carry : std::nullopt;
        if (record.Made())
        {
            if (carryOver)
            {
                const Backend::VehicleRead read = m_backend->ReadVehicle(record.id);
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
            m_backend->DestroyVehicle(record.id);
        }
        // What was warned about stays warned about: a rebuild is no reason to warn again.
        const std::uint32_t warned = record.warned;
        record = VehicleRecord{};
        record.warned = warned;
        record.carry = std::move(carry);
    }

    std::string Vehicles::WhyNoChassis(std::uint32_t chassis) const
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
        const Bodies::Body* body = m_bodies->BodyOf(chassis);
        if (body == nullptr || !body->Active())
        {
            return "its RigidBody has no collider physics could make";
        }
        if (body->isTrigger)
        {
            return "its collider is a trigger, which gives its RigidBody no solid collider";
        }
        if (body->motion.motionType != MotionType::Dynamic)
        {
            return body->motion.motionType == MotionType::Kinematic ? "its RigidBody is kinematic"
                                                                    : "its RigidBody is static";
        }
        return {};
    }

    const Bodies::Body* Vehicles::ChassisBodyOf(std::uint32_t chassis) const
    {
        return WhyNoChassis(chassis).empty() ? m_bodies->BodyOf(chassis) : nullptr;
    }

    std::string Vehicles::WhyNoWheel(std::uint32_t wheel) const
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

    std::map<std::uint32_t, std::vector<std::uint32_t>> Vehicles::WheelsByVehicle() const
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

    void Vehicles::WarnAboutInactiveVehicles()
    {
        m_inactiveVehicles.Keep(m_vehicleEntities.Entities());
        for (const std::uint32_t id : m_vehicleEntities.Entities())
        {
            m_inactiveVehicles.Report(id, WhyNoChassis(id));
        }
        m_inactiveWheels.Keep(m_wheelEntities.Entities());
        for (const std::uint32_t id : m_wheelEntities.Entities())
        {
            m_inactiveWheels.Report(id, WhyNoWheel(id));
        }
    }

    void Vehicles::Sync()
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
            const Bodies::Body* body = ChassisBodyOf(chassis);
            const std::vector<std::uint32_t>& wheels = wheelsOf[chassis];
            if (!vehicle.enabled || body == nullptr || wheels.empty())
            {
                // Disabled, inactive, or with nothing to drive it: a plain rigid body, if any, its state reset in the
                // write-back. Enabled and valid again, it is built fresh: the record holds nothing to carry over.
                DropVehicle(record, false);
                continue;
            }

            const VehicleLinks& links = linksOf[chassis];
            const Math::Vec3& scale = m_poses->Baseline(chassis).scale;
            bool wake = false;
            if (!record.Made() || record.desc.body.value != body->id.value || record.wheels != wheels)
            {
                // A wheel-set change, or the chassis's body rebuilt: what still maps carries over, a new wheel still.
                DropVehicle(record, true);
                Backend::VehicleDesc desc;
                desc.body = body->id;
                desc.drive = DriveDescOf(vehicle.kind, chassis, record.warned, *m_logger);
                desc.probe = vehicle.probe;
                desc.maxTilt = TiltOf(vehicle);
                desc.priority = chassis;
                desc.differentials = links.differentials;
                desc.antiRollBars = links.antiRollBars;
                for (const std::uint32_t wheel : wheels)
                {
                    const VehicleWheel& settings = m_entities->GetEntity(wheel).GetComponent<VehicleWheel>();
                    desc.wheels.push_back(WheelDesc(wheel, settings, scale));
                    record.writtenWheels.push_back(settings);
                }
                if (record.carry)
                {
                    desc.carried =
                        Backend::VehicleCarry{.engineRpm = record.carry->engineRpm,
                                              .gear = std::min(record.carry->gear, desc.drive.transmission.gearCount),
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
                record.id = m_backend->CreateVehicle(desc);
                desc.carried.reset();
                record.wheels = wheels;
                record.desc = std::move(desc);
                ShowMade(chassis, record);
                wake = true;
            }
            else
            {
                wake = UpdateVehicle(chassis, record, vehicle, links, scale);
            }

            // Any change wakes it, even one its ranges clamp away.
            wake = wake || !Same(vehicle.input, record.written.input);
            m_backend->SetVehicleInput(record.id, Clamped(vehicle.input, record.desc.drive), wake);
            record.written = vehicle;
        }
    }

    Backend::VehicleWheelDesc Vehicles::WheelDesc(std::uint32_t entity, const VehicleWheel& wheel,
                                                  const Math::Vec3& scale)
    {
        return WheelDescOf(entity, wheel, scale, m_wheelWarnings[entity], *m_logger);
    }

    bool Vehicles::UpdateVehicle(std::uint32_t chassis, VehicleRecord& record, const Vehicle& vehicle,
                                 const VehicleLinks& links, const Math::Vec3& scale)
    {
        Backend::VehicleDesc& desc = record.desc;
        bool changed = false;
        // Corrected, and so warned about, only when edited.
        if (!Same(vehicle.kind, record.written.kind))
        {
            desc.drive = DriveDescOf(vehicle.kind, chassis, record.warned, *m_logger);
            changed = true;
        }
        for (std::size_t i = 0; i < record.wheels.size(); ++i)
        {
            const VehicleWheel& wheel = m_entities->GetEntity(record.wheels[i]).GetComponent<VehicleWheel>();
            // A wheel's position follows the chassis's scale, so a scale change moves it, as an edit does.
            if (Same(wheel, record.writtenWheels[i]) && Fenrir::Same(wheel.position * scale, desc.wheels[i].position))
            {
                continue;
            }
            desc.wheels[i] = WheelDesc(record.wheels[i], wheel, scale);
            record.writtenWheels[i] = wheel;
            changed = true;
        }
        if (vehicle.probe != record.written.probe)
        {
            desc.probe = vehicle.probe;
            changed = true;
        }
        if (!Fenrir::Same(vehicle.maxTilt, record.written.maxTilt))
        {
            desc.maxTilt = TiltOf(vehicle);
            changed = true;
        }
        // Differential and anti-roll-bar edits and relinks.
        if (!Same(links.differentials, desc.differentials))
        {
            desc.differentials = links.differentials;
            changed = true;
        }
        if (!Same(links.antiRollBars, desc.antiRollBars))
        {
            desc.antiRollBars = links.antiRollBars;
            changed = true;
        }
        // Made again by the backend, it is shown as a new vehicle is.
        if (changed && m_backend->UpdateVehicle(record.id, desc))
        {
            ShowMade(chassis, record);
        }
        return changed;
    }

    void Vehicles::ShowMade(std::uint32_t chassis, VehicleRecord& record)
    {
        record.parked.reset();
        record.previous.clear();
        record.current.clear();
        record.snap = true;
        HandWheels(chassis, record, m_backend->ReadVehicle(record.id));
        m_poses->WriteBack();
    }

    void Vehicles::HandWheels(std::uint32_t chassis, const VehicleRecord& record, const Backend::VehicleRead& read)
    {
        for (std::size_t i = 0; i < record.wheels.size(); ++i)
        {
            m_poses->HandWheel(record.wheels[i], chassis, WheelPose(record.desc.wheels[i], read.wheels[i]));
        }
    }

    void Vehicles::WriteBack(float dt)
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
            if (!m_backend->IsSleeping(record.desc.body))
            {
                record.parked.reset();
            }
            else if (!record.parked)
            {
                record.parked = m_backend->ReadVehicle(record.id);
            }
            const Backend::VehicleRead read = record.parked ? *record.parked : m_backend->ReadVehicle(record.id);
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
            // Poses writes them after the chassis, against its step pose.
            HandWheels(chassis, record, read);
            AdvanceWheels(record, read, dt, m_poses->Snaps(chassis));
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

    void Vehicles::AdvanceWheels(VehicleRecord& record, const Backend::VehicleRead& read, float dt, bool teleported)
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

    void Vehicles::BlendWheels(float fraction)
    {
        for (auto& [chassis, record] : m_vehicles)
        {
            // Shown as its chassis is: only a chassis Blend blends (interpolated, and not held by game code) carries
            // its wheels between steps. Otherwise they stay at their step poses.
            const std::optional<Transform> at = m_poses->BlendedPose(chassis, fraction);
            if (!record.Made() || record.current.size() != record.wheels.size() ||
                m_bodies->BodyOf(chassis) == nullptr || !at)
            {
                continue;
            }
            // A wheel entity destroyed since the last step, in a frame with no tick: its vehicle is rebuilt in the next
            // drain, and until then its wheels stay at their step poses.
            if (!std::ranges::all_of(record.wheels, [this](std::uint32_t id) { return m_entities->HasEntity(id); }))
            {
                continue;
            }
            for (std::size_t i = 0; i < record.wheels.size(); ++i)
            {
                const VehicleRecord::WheelShown& from = record.previous[i];
                const VehicleRecord::WheelShown& to = record.current[i];
                Backend::VehicleWheelRead between;
                between.compression = from.compression + (to.compression - from.compression) * fraction;
                between.steerAngle = from.steer + (to.steer - from.steer) * fraction;
                between.spinAngle = from.spin + (to.spin - from.spin) * fraction;
                const Transform local = WheelPose(record.desc.wheels[i], between);
                Transform world = *at;
                world.pos = at->pos + at->rot * local.pos;
                world.rot = at->rot * local.rot;
                m_poses->HandBlendedWheel(record.wheels[i], chassis, world);
            }
        }
    }

    void Vehicles::Draw(DebugCanvas& canvas) const
    {
        // Only vehicles with a backend vehicle: disabled and inactive ones have none, and draw nothing.
        for (const auto& [chassis, record] : m_vehicles)
        {
            if (!record.Made())
            {
                continue;
            }
            const Transform& body = m_poses->Baseline(chassis);
            const Backend::VehicleRead read = m_backend->ReadVehicle(record.id);
            for (std::size_t i = 0; i < record.desc.wheels.size(); ++i)
            {
                const Backend::VehicleWheelDesc& given = record.desc.wheels[i];
                const Backend::VehicleWheelRead& wheel = read.wheels[i];
                const Transform pose = WheelPose(given, wheel);

                // The suspension in the chassis's axes: from the top of its travel down to full extension, with a
                // cross-tick across it at the wheel's centre, in the axes of the wheel's mount.
                const LineWriter suspension = canvas.Writer(body.pos, body.rot, DebugColour::Suspension);
                suspension(given.position + given.steeringAxis * given.suspension.travel, given.position);
                for (const Math::Vec3& axis : {xAxis, zAxis})
                {
                    const Math::Vec3 across = suspensionTick * (given.rest * axis);
                    suspension(pose.pos - across, pose.pos + across);
                }

                // The wheel in its own plane, Y and Z about its axle X, with a spoke up its Y.
                const LineWriter rim =
                    canvas.Writer(body.pos + body.rot * pose.pos, body.rot * pose.rot, DebugColour::Wheel);
                const float radius = given.radius;
                rim.Arc(Math::Vec3(0.0f), yAxis, zAxis, radius, 0.0f, 2.0f * pi, circleSegments);
                rim(Math::Vec3(0.0f), radius * yAxis);

                if (wheel.onGround)
                {
                    canvas.ContactPoint(wheel.contactPoint, wheel.contactNormal, GripColour(given, wheel));
                }
            }
        }
    }
} // namespace Fenrir::Detail
