// A physics world's vehicles (ADR-0015): each a backend vehicle on its chassis's body, removed with the joints before
// bodies go, made and given its driver input after the joints in PostTick step 2, run inside the step by the backend,
// and written back after the bodies in step 5, its wheel entities posed for drawing. Knows the ECS, not Jolt: it talks
// to Backend::World.

#include <algorithm>
#include <cstdint>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <tuple>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
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

        bool Same(const VehicleInput& a, const VehicleInput& b)
        {
            return Fenrir::Same(a.forward, b.forward) && Fenrir::Same(a.steer, b.steer) &&
                   Fenrir::Same(a.brake, b.brake) && Fenrir::Same(a.handbrake, b.handbrake) &&
                   Fenrir::Same(a.leftTrack, b.leftTrack) && Fenrir::Same(a.rightTrack, b.rightTrack) &&
                   a.gear == b.gear && Fenrir::Same(a.clutch, b.clutch);
        }

        // Driver input in its ranges, silently, as joint position targets are.
        VehicleInput Clamped(const VehicleInput& input)
        {
            VehicleInput clamped = input;
            clamped.forward = std::clamp(input.forward, -1.0f, 1.0f);
            clamped.steer = std::clamp(input.steer, -1.0f, 1.0f);
            clamped.brake = std::clamp(input.brake, 0.0f, 1.0f);
            clamped.handbrake = std::clamp(input.handbrake, 0.0f, 1.0f);
            return clamped;
        }

        // Just below a quarter turn: the most a wheel steers.
        constexpr float maxAngle = 0.4999f * std::numbers::pi_v<float>;
        constexpr float minRadius = 0.01f;
        // Just above 1: a limited-slip ratio of 1 or less locks the differential.
        constexpr float minLimitedSlipRatio = 1.001f;

        // A wheel's settings corrected into the ranges the backend can use.
        VehicleWheel InRange(const VehicleWheel& wheel)
        {
            VehicleWheel corrected = wheel;
            for (float* value :
                 {&corrected.width, &corrected.inertia, &corrected.angularDamping, &corrected.suspension.travel,
                  &corrected.suspension.preload, &corrected.suspension.spring.frequency,
                  &corrected.suspension.spring.damping, &corrected.maxBrakeTorque, &corrected.maxHandbrakeTorque})
            {
                *value = std::max(*value, 0.0f);
            }
            corrected.radius = std::max(corrected.radius, minRadius);
            corrected.maxSteerAngle = std::clamp(corrected.maxSteerAngle, -maxAngle, maxAngle);
            for (VehicleTyre* tyre : {&corrected.longitudinal, &corrected.lateral})
            {
                tyre->peakSlip = std::max(tyre->peakSlip, 0.0f);
                tyre->slideSlip = std::max(tyre->slideSlip, tyre->peakSlip + 1e-4f);
                tyre->peakFriction = std::max(tyre->peakFriction, 0.0f);
                tyre->slideFriction = std::max(tyre->slideFriction, 0.0f);
            }
            return corrected;
        }

        // An engine and gearbox corrected into the ranges the backend can use.
        void Correct(VehicleEngine& engine, VehicleTransmission& transmission)
        {
            for (float* value : {&engine.maxTorque, &engine.minRpm, &engine.inertia, &engine.angularDamping,
                                 &engine.torqueAtMinRpm, &engine.torqueAtMaxRpm, &transmission.switchTime,
                                 &transmission.clutchReleaseTime, &transmission.switchLatency})
            {
                *value = std::max(*value, 0.0f);
            }
            engine.maxRpm = std::max(engine.maxRpm, engine.minRpm + 1.0f);
            engine.peakTorqueRpm = std::clamp(engine.peakTorqueRpm, engine.minRpm, engine.maxRpm);
            transmission.gearCount = std::max(transmission.gearCount, 1);
            for (float* ratio : {&transmission.firstGear, &transmission.topGear, &transmission.reverseGear})
            {
                *ratio = std::max(*ratio, 1e-3f);
            }
            // Shifting up below the top of the range and down above the bottom, with room between.
            transmission.shiftUpRpm = std::clamp(transmission.shiftUpRpm, engine.minRpm + 0.5f, engine.maxRpm - 0.5f);
            transmission.shiftDownRpm = std::clamp(transmission.shiftDownRpm, std::max(engine.minRpm, 0.5f) - 0.25f,
                                                   transmission.shiftUpRpm - 0.25f);
            transmission.clutchStrength = std::max(transmission.clutchStrength, 1e-3f);
        }

        VehicleKind InRange(const VehicleKind& kind)
        {
            VehicleKind corrected = kind;
            WheeledVehicle& wheeled = corrected.As<WheeledVehicle>();
            Correct(wheeled.engine, wheeled.transmission);
            wheeled.limitedSlipRatio = std::max(wheeled.limitedSlipRatio, minLimitedSlipRatio);
            return corrected;
        }

        // A wheel's pose relative to its chassis's origin, in the chassis's axes: its position raised by its
        // compression, steered about +Y, then spun about its axle, X (rolling forward turns its top towards -Z).
        Transform WheelPose(const Math::Vec3& position, const Backend::VehicleWheelRead& read)
        {
            const Math::Quat steer = Math::AngleAxis(-read.steerAngle, Math::Vec3(0.0f, 1.0f, 0.0f));
            const Math::Quat spin = Math::AngleAxis(-read.spinAngle, Math::Vec3(1.0f, 0.0f, 0.0f));
            Transform pose;
            pose.pos = position + Math::Vec3(0.0f, read.compression, 0.0f);
            pose.rot = steer * spin;
            return pose;
        }
    } // namespace

    void PhysicsWorld::Impl::DrainVehicles()
    {
        const auto track = [this](ComponentWatch& watch, std::set<std::uint32_t>& tracked, auto has, auto changed) {
            for (const ComponentChange& change : watch.Drain())
            {
                changed(change.entity);
                if (m_entities->HasEntity(change.entity) && has(m_entities->GetEntity(change.entity)))
                {
                    tracked.insert(change.entity);
                }
                else
                {
                    tracked.erase(change.entity);
                }
            }
        };
        // A Vehicle removed, or removed and added again, loses its backend vehicle: it is made again from what the
        // entity has now.
        track(
            m_vehicleWatch, m_vehicleEntities, [](const Entity& e) { return e.HasComponent<Vehicle>(); },
            [this](std::uint32_t entity) {
                if (const auto it = m_vehicles.find(entity); it != m_vehicles.end())
                {
                    DropVehicle(it->second);
                    m_vehicles.erase(it);
                }
            });
        // A wheel that came or went, even one that came back, changes its vehicle's wheel set.
        track(
            m_wheelWatch, m_wheelEntities, [](const Entity& e) { return e.HasComponent<VehicleWheel>(); },
            [this](std::uint32_t entity) {
                for (auto& [chassis, record] : m_vehicles)
                {
                    if (std::ranges::find(record.wheels, entity) != record.wheels.end())
                    {
                        DropVehicle(record);
                    }
                }
            });
    }

    void PhysicsWorld::Impl::RemoveVehiclesOf(std::span<const Backend::BodyId> bodies)
    {
        for (auto& [chassis, record] : m_vehicles)
        {
            if (record.Made() && std::ranges::any_of(bodies, [&record](const Backend::BodyId body) {
                    return body.value == record.body.value;
                }))
            {
                DropVehicle(record);
            }
        }
    }

    void PhysicsWorld::Impl::DropVehicle(VehicleRecord& record)
    {
        if (record.Made())
        {
            m_backend.DestroyVehicle(record.id);
        }
        record = VehicleRecord{};
    }

    const PhysicsWorld::Impl::Record* PhysicsWorld::Impl::ChassisBodyOf(std::uint32_t chassis) const
    {
        const Entity entity = m_entities->GetEntity(chassis);
        if (entity.HasComponent<CharacterBody>() || !entity.HasComponent<RigidBody>())
        {
            return nullptr;
        }
        const auto it = m_bodies.find(chassis);
        if (it == m_bodies.end() || !it->second.Active() || it->second.isTrigger ||
            it->second.motion.motionType != MotionType::Dynamic)
        {
            return nullptr;
        }
        return &it->second;
    }

    std::map<std::uint32_t, std::vector<std::uint32_t>> PhysicsWorld::Impl::WheelsByVehicle() const
    {
        std::map<std::uint32_t, std::vector<std::uint32_t>> wheels;
        for (const std::uint32_t id : m_wheelEntities)
        {
            const Entity entity = m_entities->GetEntity(id);
            const Entity& chassis = entity.GetComponent<VehicleWheel>().vehicle;
            // A wheel names a vehicle of its own scene, and is no body itself.
            if (!chassis.IsValid() || chassis != m_entities->HandleOf(chassis.GetId()) ||
                !m_vehicleEntities.contains(chassis.GetId()) || chassis.GetId() == id ||
                entity.HasComponent<RigidBody>() || entity.HasComponent<CharacterBody>() ||
                entity.HasComponent<Collider>())
            {
                continue;
            }
            // In entity order, as m_wheelEntities is.
            wheels[chassis.GetId()].push_back(id);
        }
        return wheels;
    }

    void PhysicsWorld::Impl::SyncVehicles()
    {
        std::map<std::uint32_t, std::vector<std::uint32_t>> wheelsOf = WheelsByVehicle();
        // In chassis entity order, so vehicles are made, and so solved, in a reproducible order.
        for (const std::uint32_t chassis : m_vehicleEntities)
        {
            VehicleRecord& record = m_vehicles[chassis];
            const Record* body = ChassisBodyOf(chassis);
            const std::vector<std::uint32_t>& wheels = wheelsOf[chassis];
            if (body == nullptr || wheels.empty())
            {
                // No body to drive, or nothing to drive it with: a plain rigid body, if any.
                DropVehicle(record);
                continue;
            }

            Entity entity = m_entities->GetEntity(chassis);
            const Vehicle& vehicle = entity.GetComponent<Vehicle>();
            bool wake = false;
            if (!record.Made() || record.body.value != body->id.value || record.wheels != wheels)
            {
                DropVehicle(record);
                Backend::VehicleDesc desc;
                desc.body = body->id;
                desc.kind = InRange(vehicle.kind);
                desc.priority = chassis;
                for (const std::uint32_t wheel : wheels)
                {
                    const VehicleWheel& settings = m_entities->GetEntity(wheel).GetComponent<VehicleWheel>();
                    desc.wheels.push_back(Backend::VehicleWheelDesc{.position = settings.position * body->world.scale,
                                                                    .settings = InRange(settings)});
                }
                record.id = m_backend.CreateVehicle(desc);
                record.body = body->id;
                record.wheels = wheels;
                for (const Backend::VehicleWheelDesc& wheel : desc.wheels)
                {
                    record.positions.push_back(wheel.position);
                }
                PoseWheels(record, m_backend.ReadVehicle(record.id), body->world);
                wake = true;
            }

            const VehicleInput input = Clamped(vehicle.input);
            wake = wake || !Same(input, record.input);
            m_backend.SetVehicleInput(record.id, input, wake);
            record.input = input;
        }
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
            const Transform local = WheelPose(record.positions[i], read.wheels[i]);
            // The scale stays game code's.
            Transform world = GetWorldTransform(wheel);
            world.pos = chassis.pos + chassis.rot * local.pos;
            world.rot = chassis.rot * local.rot;
            SetWorldTransform(wheel, world);
        }
    }

    void PhysicsWorld::Impl::WriteBackVehicles()
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
            const Backend::VehicleRead read = m_backend.ReadVehicle(record.id);
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
            PoseWheels(record, read, m_bodies.at(chassis).world);
        }
        // A wheel of no made vehicle: its state reset, and its Transform left where it was.
        for (const std::uint32_t wheel : m_wheelEntities)
        {
            if (!posed.contains(wheel))
            {
                m_entities->GetEntity(wheel).GetComponent<VehicleWheel>().state = VehicleWheelState{};
            }
        }
    }
} // namespace Fenrir
