// PROTOTYPE (prototype/fenrir-physics-vehicles-spike, never merged): a physics world's vehicles (ADR-0015), synced in
// PostTick step 2 after the joints, run inside the step by the backend, written back in step 5 after the bodies, and
// their wheels blended in AfterTicks. Knows the ECS, not Jolt: it talks to Backend::World.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Every value of a component as bits, to see edits without comparing padding.
        struct Fingerprint
        {
            std::vector<std::uint32_t> bits;

            Fingerprint& operator<<(float value)
            {
                bits.push_back(std::bit_cast<std::uint32_t>(value));
                return *this;
            }
            Fingerprint& operator<<(int value)
            {
                bits.push_back(static_cast<std::uint32_t>(value));
                return *this;
            }
            Fingerprint& operator<<(bool value)
            {
                bits.push_back(value ? 1u : 0u);
                return *this;
            }
            Fingerprint& operator<<(const Math::Vec3& v)
            {
                return *this << v.x << v.y << v.z;
            }
        };

        void Add(Fingerprint& f, const VehicleEngine& e)
        {
            f << e.maxTorque << e.minRPM << e.maxRPM << e.peakTorqueRPM << e.torqueAtMinRPM << e.torqueAtMaxRPM
              << e.inertia << e.angularDamping;
        }

        void Add(Fingerprint& f, const VehicleTransmission& t)
        {
            f << static_cast<int>(t.mode) << t.gearCount << t.firstGear << t.topGear << t.reverseGear << t.shiftUpRPM
              << t.shiftDownRPM << t.switchTime << t.clutchReleaseTime << t.switchLatency << t.clutchStrength;
        }

        void Add(Fingerprint& f, const VehicleTrack& t)
        {
            f << t.inertia << t.angularDamping << t.maxBrakeTorque << t.differentialRatio << t.longitudinalFriction
              << t.lateralFriction;
        }

        void Add(Fingerprint& f, const VehicleTyre& t)
        {
            f << t.peakSlip << t.peakFriction << t.slideSlip << t.slideFriction;
        }

        void Add(Fingerprint& f, const Backend::VehicleDesc& desc)
        {
            f << static_cast<int>(desc.kind.Type()) << static_cast<int>(desc.probe) << desc.maxTilt;
            switch (desc.kind.Type())
            {
            case VehicleType::Wheeled:
                Add(f, desc.kind.As<WheeledVehicle>().engine);
                Add(f, desc.kind.As<WheeledVehicle>().transmission);
                f << desc.kind.As<WheeledVehicle>().limitedSlipRatio;
                break;
            case VehicleType::Motorcycle: {
                const MotorcycleVehicle& bike = desc.kind.As<MotorcycleVehicle>();
                Add(f, bike.engine);
                Add(f, bike.transmission);
                f << bike.limitedSlipRatio << bike.lean.enabled << bike.lean.maxAngle << bike.lean.limitSteering;
                break;
            }
            case VehicleType::Tracked:
            default:
                Add(f, desc.kind.As<TrackedVehicle>().engine);
                Add(f, desc.kind.As<TrackedVehicle>().transmission);
                Add(f, desc.kind.As<TrackedVehicle>().left);
                Add(f, desc.kind.As<TrackedVehicle>().right);
                break;
            }
            for (const Backend::VehicleWheelDesc& each : desc.wheels)
            {
                const VehicleWheel& w = each.wheel;
                f << each.position << static_cast<int>(w.track) << w.radius << w.width << w.inertia << w.angularDamping
                  << w.caster << w.camber << w.toe << w.suspension.travel << w.suspension.spring.frequency
                  << w.suspension.spring.damping << w.suspension.preload << w.maxSteerAngle << w.maxBrakeTorque
                  << w.maxHandbrakeTorque;
                Add(f, w.longitudinal);
                Add(f, w.lateral);
            }
            for (const Backend::VehicleDifferentialDesc& d : desc.differentials)
            {
                f << d.left << d.right << d.settings.ratio << d.settings.leftRightSplit << d.settings.limitedSlipRatio
                  << d.settings.torqueShare;
            }
            f << -1;
            for (const Backend::VehicleAntiRollBarDesc& bar : desc.antiRollBars)
            {
                f << bar.left << bar.right << bar.stiffness;
            }
        }

        std::vector<std::uint32_t> InputBits(const VehicleInput& input)
        {
            Fingerprint f;
            f << input.forward << input.steer << input.brake << input.handbrake << input.leftTrack << input.rightTrack
              << input.gear << input.clutch;
            return std::move(f.bits);
        }

        float MaxLeanOf(const VehicleKind& kind)
        {
            return kind.Is<MotorcycleVehicle>() ? kind.As<MotorcycleVehicle>().lean.maxAngle : 0.0f;
        }

        // Why the wheel on `entity` cannot be a wheel of a vehicle now, or empty.
        std::string WhyNoWheel(const Entity& entity)
        {
            const VehicleWheel& wheel = entity.GetComponent<VehicleWheel>();
            if (!wheel.vehicle.IsValid() || !wheel.vehicle.HasComponent<Vehicle>())
            {
                return "it names no entity with a Vehicle";
            }
            if (wheel.vehicle == entity)
            {
                return "it sits on its chassis's entity";
            }
            if (entity.HasComponent<RigidBody>() || entity.HasComponent<CharacterBody>() ||
                entity.HasComponent<Collider>())
            {
                return "its entity has a RigidBody, CharacterBody or Collider";
            }
            return {};
        }

        // The spin closest to `previous + angularVelocity * dt` with the wrapped angle `wrapped`.
        float Unwrap(float previousUnwrapped, float previousWrapped, float wrapped, float angularVelocity, float dt)
        {
            constexpr float turn = 2.0f * std::numbers::pi_v<float>;
            const float delta = wrapped - previousWrapped;
            const float expected = angularVelocity * dt;
            return previousUnwrapped + delta - turn * std::round((delta - expected) / turn);
        }
    } // namespace

    void PhysicsWorld::Impl::RemoveVehiclesOf(std::span<const Backend::BodyId> bodies)
    {
        for (auto& [entity, record] : m_vehicles)
        {
            if (!record.Made())
            {
                continue;
            }
            for (const Backend::BodyId body : bodies)
            {
                if (body.value == record.body.value)
                {
                    record.rpm = record.read.state.engineRpm;
                    record.gear = record.read.state.gear;
                    m_backend.DestroyVehicle(record.id);
                    record.id = Backend::VehicleId{};
                    break;
                }
            }
        }
    }

    void PhysicsWorld::Impl::SyncVehicles()
    {
        // In entity order: vehicles by chassis, wheels in their vehicle's order.
        std::vector<std::uint32_t> chassisIds;
        const ComponentType vehicles[] = {ComponentType::Native<Vehicle>()};
        m_entities->ForEachWith(vehicles, [&chassisIds](std::uint32_t id) { chassisIds.push_back(id); });
        std::ranges::sort(chassisIds);
        std::vector<std::uint32_t> wheelIds;
        const ComponentType wheels[] = {ComponentType::Native<VehicleWheel>()};
        m_entities->ForEachWith(wheels, [&wheelIds](std::uint32_t id) { wheelIds.push_back(id); });
        std::ranges::sort(wheelIds);
        std::vector<std::uint32_t> differentialIds;
        const ComponentType differentials[] = {ComponentType::Native<VehicleDifferential>()};
        m_entities->ForEachWith(differentials, [&differentialIds](std::uint32_t id) { differentialIds.push_back(id); });
        std::ranges::sort(differentialIds);
        std::vector<std::uint32_t> barIds;
        const ComponentType bars[] = {ComponentType::Native<VehicleAntiRollBar>()};
        m_entities->ForEachWith(bars, [&barIds](std::uint32_t id) { barIds.push_back(id); });
        std::ranges::sort(barIds);

        std::map<std::uint32_t, std::vector<std::uint32_t>> wheelsOf;
        for (const std::uint32_t id : wheelIds)
        {
            const Entity entity = m_entities->GetEntity(id);
            const std::string why = WhyNoWheel(entity);
            if (!why.empty())
            {
                if (m_inactiveVehicles.insert(id).second)
                {
                    m_logger->Warn("The VehicleWheel of entity " + std::to_string(id) + " is inactive: " + why);
                }
                continue;
            }
            m_inactiveVehicles.erase(id);
            wheelsOf[entity.GetComponent<VehicleWheel>().vehicle.GetId()].push_back(id);
        }

        // Gone: no Vehicle any more.
        for (auto it = m_vehicles.begin(); it != m_vehicles.end();)
        {
            if (std::ranges::binary_search(chassisIds, it->first))
            {
                ++it;
                continue;
            }
            if (it->second.Made())
            {
                m_backend.DestroyVehicle(it->second.id);
            }
            it = m_vehicles.erase(it);
        }

        for (const std::uint32_t id : chassisIds)
        {
            Entity chassis = m_entities->GetEntity(id);
            Vehicle& vehicle = chassis.GetComponent<Vehicle>();
            VehicleRecord& record = m_vehicles[id];

            const Record* body = nullptr;
            if (const auto it = m_bodies.find(id); it != m_bodies.end() && it->second.Active())
            {
                body = &it->second;
            }
            std::string why;
            if (body == nullptr || !chassis.HasComponent<RigidBody>() ||
                chassis.GetComponent<RigidBody>().motionType != MotionType::Dynamic)
            {
                why = "its entity has no made dynamic RigidBody";
            }
            else if (chassis.HasComponent<CharacterBody>())
            {
                why = "its entity has a CharacterBody";
            }
            if (!why.empty() && m_inactiveVehicles.insert(id).second)
            {
                m_logger->Warn("The Vehicle of entity " + std::to_string(id) + " is inactive: " + why);
            }
            if (why.empty())
            {
                m_inactiveVehicles.erase(id);
            }
            const std::vector<std::uint32_t>& ownWheels = wheelsOf[id];
            // Disabled, inactive or wheelless: a plain rigid body, nothing carried into a later build.
            if (!vehicle.enabled || !why.empty() || ownWheels.empty())
            {
                if (record.Made())
                {
                    m_backend.DestroyVehicle(record.id);
                }
                record = VehicleRecord{};
                continue;
            }

            Backend::VehicleDesc desc;
            desc.body = body->id;
            desc.kind = vehicle.kind;
            desc.probe = vehicle.probe;
            desc.maxTilt = vehicle.maxTilt;
            desc.priority = id;
            const Math::Vec3 scale = GetWorldTransform(chassis).scale;
            for (const std::uint32_t wheelId : ownWheels)
            {
                Backend::VehicleWheelDesc each;
                each.wheel = m_entities->GetEntity(wheelId).GetComponent<VehicleWheel>();
                each.position = each.wheel.position * scale;
                each.wheel.radius = std::max(each.wheel.radius, 0.01f);
                const auto spin = record.spins.find(wheelId);
                each.spin = spin == record.spins.end() ? 0.0f : spin->second;
                desc.wheels.push_back(each);
            }
            const auto indexOf = [&ownWheels](const Entity& wheel) {
                if (!wheel.IsValid())
                {
                    return -1;
                }
                const auto it = std::ranges::find(ownWheels, wheel.GetId());
                return it == ownWheels.end() ? -2 : static_cast<int>(it - ownWheels.begin());
            };
            if (vehicle.kind.Type() != VehicleType::Tracked)
            {
                for (const std::uint32_t differentialId : differentialIds)
                {
                    const VehicleDifferential& d =
                        m_entities->GetEntity(differentialId).GetComponent<VehicleDifferential>();
                    const int left = indexOf(d.leftWheel);
                    const int right = indexOf(d.rightWheel);
                    if (left == -2 || right == -2 || (left == -1 && right == -1))
                    {
                        continue; // another vehicle's, or invalid
                    }
                    desc.differentials.push_back(Backend::VehicleDifferentialDesc{left, right, d});
                }
            }
            for (const std::uint32_t barId : barIds)
            {
                const VehicleAntiRollBar& bar = m_entities->GetEntity(barId).GetComponent<VehicleAntiRollBar>();
                const int left = indexOf(bar.leftWheel);
                const int right = indexOf(bar.rightWheel);
                if (left < 0 || right < 0)
                {
                    continue;
                }
                desc.antiRollBars.push_back(Backend::VehicleAntiRollBarDesc{left, right, bar.stiffness});
            }

            Fingerprint settings;
            Add(settings, desc);
            const bool rebuild = !record.Made() || record.body.value != body->id.value ||
                                 record.type != vehicle.kind.Type() || record.wheels != ownWheels ||
                                 record.maxLean != MaxLeanOf(vehicle.kind);
            bool wake = false;
            if (rebuild)
            {
                if (record.Made())
                {
                    // Carries what still maps: RPM, gear, and each surviving wheel's spin.
                    record.rpm = record.read.state.engineRpm;
                    record.gear = record.read.state.gear;
                    m_backend.DestroyVehicle(record.id);
                }
                desc.rpm = record.rpm;
                desc.gear = record.gear;
                record.id = m_backend.CreateVehicle(desc);
                record.body = body->id;
                record.type = vehicle.kind.Type();
                record.maxLean = MaxLeanOf(vehicle.kind);
                record.wheels = ownWheels;
                record.settings = settings.bits;
                record.previous.assign(ownWheels.size(), WheelPoseState{});
                record.current.assign(ownWheels.size(), WheelPoseState{});
                record.wrappedSpin.assign(ownWheels.size(), 0.0f);
                record.snap = true;
                record.input.clear();
                wake = true;
            }
            else if (record.settings != settings.bits)
            {
                m_backend.UpdateVehicle(record.id, desc);
                record.settings = settings.bits;
                wake = true;
            }
            std::vector<std::uint32_t> input = InputBits(vehicle.input);
            wake = wake || input != record.input;
            m_backend.SetVehicleInput(record.id, vehicle.input, wake);
            record.input = std::move(input);
        }
    }

    void PhysicsWorld::Impl::WriteBackVehicles(float dt)
    {
        const auto handle = [this](std::uint32_t id) { return id == ~0u ? Entity() : m_entities->HandleOf(id); };
        for (auto& [id, record] : m_vehicles)
        {
            Entity chassis = m_entities->GetEntity(id);
            Vehicle& vehicle = chassis.GetComponent<Vehicle>();
            if (!record.Made())
            {
                vehicle.state = VehicleState{};
                continue;
            }
            record.read = m_backend.ReadVehicle(record.id);
            vehicle.state = record.read.state;
            const Record& body = m_bodies.at(id);
            const bool snap = record.snap || body.shown.snap;
            record.snap = false;
            for (std::size_t i = 0; i < record.wheels.size(); ++i)
            {
                const Backend::VehicleWheelRead& read = record.read.wheels[i];
                Entity wheelEntity = m_entities->GetEntity(record.wheels[i]);
                VehicleWheel& wheel = wheelEntity.GetComponent<VehicleWheel>();
                wheel.state = VehicleWheelState{.onGround = read.onGround,
                                                .groundBody = handle(read.groundEntity),
                                                .groundCollider = handle(read.groundCollider),
                                                .contactPoint = read.contactPoint,
                                                .contactNormal = read.contactNormal,
                                                .groundVelocity = read.groundVelocity,
                                                .compression = read.compression,
                                                .suspensionForce = read.suspensionForce,
                                                .longitudinalSlip = read.longitudinalSlip,
                                                .lateralSlip = read.lateralSlip,
                                                .angularVelocity = read.angularVelocity,
                                                .steerAngle = read.steerAngle};
                record.spins[record.wheels[i]] = read.angularVelocity;

                WheelPoseState now{
                    read.compression, read.steerAngle,
                    Unwrap(record.current[i].spin, record.wrappedSpin[i], read.spinAngle, read.angularVelocity, dt)};
                record.wrappedSpin[i] = read.spinAngle;
                record.previous[i] = snap ? now : record.current[i];
                record.current[i] = now;

                const JointFrame local = m_backend.WheelPose(record.id, i, now.compression, now.steer, now.spin);
                Transform world = GetWorldTransform(wheelEntity);
                world.pos = body.world.pos + body.world.rot * local.position;
                world.rot = body.world.rot * local.rotation;
                SetWorldTransform(wheelEntity, world);
            }
        }
        // Wheels of no made vehicle: state reset, Transform left where it was.
        const ComponentType wheels[] = {ComponentType::Native<VehicleWheel>()};
        m_entities->ForEachWith(wheels, [this](std::uint32_t id) {
            VehicleWheel& wheel = m_entities->GetEntity(id).GetComponent<VehicleWheel>();
            const auto it = m_vehicles.find(wheel.vehicle.IsValid() ? wheel.vehicle.GetId() : ~0u);
            if (it == m_vehicles.end() || !it->second.Made() ||
                std::ranges::find(it->second.wheels, id) == it->second.wheels.end())
            {
                wheel.state = VehicleWheelState{};
            }
        });
    }

    void PhysicsWorld::Impl::BlendVehicles(float fraction)
    {
        for (auto& [id, record] : m_vehicles)
        {
            if (!record.Made() || !m_entities->HasEntity(id))
            {
                continue;
            }
            const Entity chassis = m_entities->GetEntity(id);
            const Record& body = m_bodies.at(id);
            // The chassis as Blend showed it.
            Transform shown = body.world;
            if (chassis.GetComponent<RigidBody>().interpolate && !body.shown.heldByGame)
            {
                shown.pos = body.shown.previous.pos + (body.shown.current.pos - body.shown.previous.pos) * fraction;
                shown.rot = Math::Slerp(body.shown.previous.rot, body.shown.current.rot, fraction);
            }
            for (std::size_t i = 0; i < record.wheels.size(); ++i)
            {
                const WheelPoseState& a = record.previous[i];
                const WheelPoseState& b = record.current[i];
                const JointFrame local = m_backend.WheelPose(
                    record.id, i, a.compression + (b.compression - a.compression) * fraction,
                    a.steer + (b.steer - a.steer) * fraction, a.spin + (b.spin - a.spin) * fraction);
                Entity wheelEntity = m_entities->GetEntity(record.wheels[i]);
                Transform world = GetWorldTransform(wheelEntity);
                world.pos = shown.pos + shown.rot * local.position;
                world.rot = shown.rot * local.rotation;
                SetWorldTransform(wheelEntity, world);
            }
        }
    }
} // namespace Fenrir
