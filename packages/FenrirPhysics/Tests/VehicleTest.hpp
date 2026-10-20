#pragma once

// What the vehicle tests share: the spike's default car, a large floor, and the game code that drives a vehicle.

#include <cmath>
#include <functional>
#include <memory>
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
