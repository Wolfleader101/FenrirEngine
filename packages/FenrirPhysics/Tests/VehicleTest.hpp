#pragma once

// What the vehicle tests share: the spike's default car, a large floor, and the game code that drives a vehicle.

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "PhysicsTestApp.hpp"

namespace PhysicsTest
{
    struct Car
    {
        Fenrir::Entity chassis;
        // Front left, front right, rear left, rear right: entity order, so wheel order.
        std::vector<Fenrir::Entity> wheels;
    };

    inline constexpr float carMass = 1500.0f;
    // Each wheel's centre at full extension, chassis-local: front at -Z.
    inline constexpr float wheelX = 0.9f;
    inline constexpr float wheelY = -0.55f;
    inline constexpr float wheelZ = 1.4f;
    inline constexpr float frontSteer = 0.5f;

    /** The spike's car: a 1500 kg unscaled box chassis 4 m long, its front wheels steering, every other setting at the
     * defaults. Its wheel entities are not its children. */
    inline Car AddCar(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& at,
                      const Fenrir::Math::Quat& rotation = identity)
    {
        Car car;
        car.chassis = Place(entities, at);
        car.chassis.GetComponent<Fenrir::Transform>().rot = rotation;
        car.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = carMass});
        car.chassis.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(0.9f, 0.25f, 2.0f)}});
        car.chassis.AddComponent<Fenrir::Vehicle>();
        for (const float z : {-wheelZ, wheelZ})
        {
            for (const float x : {-wheelX, wheelX})
            {
                const Fenrir::Math::Vec3 position(x, wheelY, z);
                Fenrir::Entity wheel = Place(entities, at + rotation * position);
                wheel.AddComponent<Fenrir::VehicleWheel>(Fenrir::VehicleWheel{
                    .vehicle = car.chassis, .position = position, .maxSteerAngle = z < 0.0f ? frontSteer : 0.0f});
                car.wheels.push_back(wheel);
            }
        }
        return car;
    }

    inline constexpr float motorcycleMass = 240.0f;
    inline constexpr float forkAngle = std::numbers::pi_v<float> / 6.0f;

    /** Jolt's sample motorcycle: a 240 kg unscaled box chassis (roll inertia 10.4 kg m²), the default
     * MotorcycleVehicle, a front wheel with a 30° fork caster that steers 30°, and a rear wheel, in that order, the
     * rear one alone driven by a differential. */
    inline Car AddMotorcycle(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& at,
                             const Fenrir::Math::Quat& rotation = identity)
    {
        Car bike;
        bike.chassis = Place(entities, at);
        bike.chassis.GetComponent<Fenrir::Transform>().rot = rotation;
        bike.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = motorcycleMass});
        bike.chassis.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(0.2f, 0.3f, 0.4f)}});
        bike.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::MotorcycleVehicle{}});
        for (const float z : {-0.75f, 0.75f})
        {
            const bool front = z < 0.0f;
            const Fenrir::Math::Vec3 position(0.0f, -0.6f, z);
            Fenrir::Entity wheel = Place(entities, at + rotation * position);
            wheel.AddComponent<Fenrir::VehicleWheel>(Fenrir::VehicleWheel{
                .vehicle = bike.chassis,
                .position = position,
                .radius = 0.31f,
                .width = 0.05f,
                .caster = front ? forkAngle : 0.0f,
                .suspension = {.travel = 0.2f, .spring = {.frequency = front ? 1.5f : 2.0f, .damping = 0.5f}},
                .maxSteerAngle = front ? forkAngle : 0.0f,
                .maxBrakeTorque = front ? 500.0f : 250.0f,
                .maxHandbrakeTorque = front ? 0.0f : 250.0f});
            bike.wheels.push_back(wheel);
        }
        Fenrir::Entity differential = entities.CreateEntity();
        differential.AddComponent<Fenrir::VehicleDifferential>(
            Fenrir::VehicleDifferential{.rightWheel = bike.wheels[1], .ratio = 1.93f * 40.0f / 16.0f});
        return bike;
    }

    inline constexpr float trailerMass = 300.0f;
    inline constexpr float dollySteer = 0.5f;

    /** The spike's trailer: a 300 kg unscaled box chassis 2 m long, a TrailerVehicle, with no handbrake. With `dolly`
     * it has two axles, its front wheels steering, and stands by itself: front left, front right, rear left, rear
     * right. Without, it has one axle at its middle (left, right), to be hitched. */
    inline Car AddTrailer(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& at, bool dolly)
    {
        Car trailer;
        trailer.chassis = Place(entities, at);
        trailer.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = trailerMass});
        trailer.chassis.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(0.8f, 0.3f, 1.0f)}});
        trailer.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::TrailerVehicle{}});
        const std::vector<float> axles = dolly ? std::vector<float>{-0.7f, 0.7f} : std::vector<float>{0.0f};
        for (const float z : axles)
        {
            for (const float x : {-0.85f, 0.85f})
            {
                const Fenrir::Math::Vec3 position(x, -0.45f, z);
                Fenrir::Entity wheel = Place(entities, at + position);
                wheel.AddComponent<Fenrir::VehicleWheel>(
                    Fenrir::VehicleWheel{.vehicle = trailer.chassis,
                                         .position = position,
                                         .maxSteerAngle = z < 0.0f ? dollySteer : 0.0f,
                                         .maxHandbrakeTorque = 0.0f});
                trailer.wheels.push_back(wheel);
            }
        }
        return trailer;
    }

    /** `trailer` behind `car`, both unturned, joined by a BallJoint on its own entity at the car's rear: the spike's
     * hitch, 0.3 m behind the car's chassis. Returns the joint's entity. */
    inline Fenrir::Entity Hitch(Fenrir::EntityList& entities, const Car& car, const Car& trailer)
    {
        const Fenrir::Math::Vec3 hitch = PositionOf(car.chassis) + Fenrir::Math::Vec3(0.0f, -0.1f, 2.3f);
        const Fenrir::JointFrames frames =
            Fenrir::FrameFromWorld(car.chassis, trailer.chassis, Fenrir::JointFrame{.position = hitch});
        Fenrir::Entity joint = entities.CreateEntity();
        joint.AddComponent<Fenrir::Joint>(Fenrir::Joint{.bodyA = car.chassis,
                                                        .bodyB = trailer.chassis,
                                                        .frameA = frames.frameA,
                                                        .frameB = frames.frameB,
                                                        .kind = Fenrir::BallJoint{}});
        return joint;
    }

    inline constexpr float tankMass = 4000.0f;
    inline constexpr int tankWheelsASide = 9;

    /** The spike's tank: a 4000 kg unscaled box chassis 6.4 m long, the default TrackedVehicle, and nine wheels a
     * side, left then right, each side front to back, the end ones rigid and higher. */
    inline Car AddTank(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& at,
                       const Fenrir::Math::Quat& rotation = identity)
    {
        Car tank;
        tank.chassis = Place(entities, at);
        tank.chassis.GetComponent<Fenrir::Transform>().rot = rotation;
        tank.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = tankMass});
        tank.chassis.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(1.7f, 0.5f, 3.2f)}});
        tank.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::TrackedVehicle{}});
        for (const float x : {-1.7f, 1.7f})
        {
            for (int i = 0; i < tankWheelsASide; ++i)
            {
                const bool end = i == 0 || i == tankWheelsASide - 1;
                const Fenrir::Math::Vec3 position(x, end ? -0.6f : -0.8f, -2.8f + 0.7f * static_cast<float>(i));
                Fenrir::Entity wheel = Place(entities, at + rotation * position);
                wheel.AddComponent<Fenrir::VehicleWheel>(Fenrir::VehicleWheel{
                    .vehicle = tank.chassis,
                    .position = position,
                    .track = x < 0.0f ? Fenrir::VehicleTrackSide::Left : Fenrir::VehicleTrackSide::Right,
                    .suspension = {.travel = end ? 0.0f : 0.2f, .spring = {.frequency = 1.0f, .damping = 0.5f}}});
                tank.wheels.push_back(wheel);
            }
        }
        return tank;
    }

    /** A static floor of `friction` whose top is at y = 0, `size` across. */
    inline Fenrir::Entity AddFloor(Fenrir::EntityList& entities, float friction = 0.5f, float size = 2000.0f)
    {
        Fenrir::Entity floor = Place(entities, Fenrir::Math::Vec3(0.0f, -0.5f, 0.0f));
        floor.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(0.5f * size, 0.5f, 0.5f * size)}});
        floor.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = friction});
        return floor;
    }

    /** Game code in Tick: `drive` writes the vehicle's input, given the tick's index from 0. */
    inline void Drive(Fenrir::App& app, const Fenrir::Entity& chassis, std::function<void(int, Fenrir::Vehicle&)> drive)
    {
        auto ticks = std::make_shared<int>(0);
        app.AddSequentialSystems(Fenrir::SchedulePriority::Tick, {[chassis, drive, ticks](Fenrir::App&) {
                                     drive((*ticks)++, chassis.GetComponent<Fenrir::Vehicle>());
                                 }});
    }

    /** Radians about world +Y that the entity's forward (-Z) has turned from -Z: + to the left. */
    inline float YawOf(const Fenrir::Entity& entity)
    {
        const Fenrir::Math::Vec3 forward =
            Fenrir::GetWorldTransform(entity).rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
        return std::atan2(-forward.x, -forward.z);
    }

    inline const Fenrir::VehicleState& StateOf(const Car& car)
    {
        return car.chassis.GetComponent<Fenrir::Vehicle>().state;
    }

    inline const Fenrir::VehicleWheelState& StateOf(const Fenrir::Entity& wheel)
    {
        return wheel.GetComponent<Fenrir::VehicleWheel>().state;
    }

    inline constexpr float kmh = 1.0f / 3.6f;

    /** Whether `value` is exactly `expected`: for values physics copies or defaults it never computes. */
    inline bool Exactly(float value, float expected)
    {
        return !(value < expected) && !(expected < value);
    }
} // namespace PhysicsTest
