// PROTOTYPE (prototype/fenrir-physics-vehicles-spike, never merged): headless checks for "Spike: a car, a motorcycle
// and a tank driving a small track through the proposed model". They print what they measure, for the ticket's answer:
// run `FenrirPhysicsTests "[vehicle-spike]" -s`.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "PhysicsTestApp.hpp"

using namespace PhysicsTest;
using Fenrir::Math::Vec3;

namespace
{
    struct Car
    {
        Fenrir::Entity chassis;
        std::vector<Fenrir::Entity> wheels;
    };

    // A 1500 kg car 4 m long, front wheels steering, rear handbrake, everything else at the defaults.
    Car AddCar(Fenrir::EntityList& entities, const Vec3& at, const Fenrir::Math::Quat& rotation = identity)
    {
        Car car;
        car.chassis = Place(entities, at);
        car.chassis.GetComponent<Fenrir::Transform>().rot = rotation;
        car.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 1500.0f});
        car.chassis.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(0.9f, 0.25f, 2.0f)}});
        car.chassis.AddComponent<Fenrir::Vehicle>();
        for (const float z : {-1.4f, 1.4f})
        {
            for (const float x : {-0.9f, 0.9f})
            {
                Fenrir::Entity wheel = entities.CreateEntity();
                const bool front = z < 0.0f;
                wheel.AddComponent<Fenrir::VehicleWheel>(
                    Fenrir::VehicleWheel{.vehicle = car.chassis,
                                         .position = Vec3(x, -0.55f, z),
                                         .maxSteerAngle = front ? 0.5f : 0.0f,
                                         .maxHandbrakeTorque = front ? 0.0f : 4000.0f});
                car.wheels.push_back(wheel);
            }
        }
        return car;
    }

    Car AddMotorcycle(Fenrir::EntityList& entities, const Vec3& at)
    {
        Car bike;
        bike.chassis = Place(entities, at);
        bike.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 240.0f});
        bike.chassis.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(0.2f, 0.3f, 0.4f)}});
        bike.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::MotorcycleVehicle{}});
        for (const float z : {-0.75f, 0.75f})
        {
            Fenrir::Entity wheel = entities.CreateEntity();
            const bool front = z < 0.0f;
            wheel.AddComponent<Fenrir::VehicleWheel>(Fenrir::VehicleWheel{
                .vehicle = bike.chassis,
                .position = Vec3(0.0f, -0.6f, z),
                .radius = 0.31f,
                .width = 0.05f,
                .caster = front ? std::numbers::pi_v<float> / 6.0f : 0.0f,
                .suspension = {.travel = 0.2f, .spring = {.frequency = front ? 1.5f : 2.0f, .damping = 0.5f}},
                .maxSteerAngle = front ? std::numbers::pi_v<float> / 6.0f : 0.0f,
                .maxBrakeTorque = front ? 500.0f : 250.0f,
                .maxHandbrakeTorque = front ? 0.0f : 250.0f});
            bike.wheels.push_back(wheel);
        }
        return bike;
    }

    Car AddTank(Fenrir::EntityList& entities, const Vec3& at)
    {
        Car tank;
        tank.chassis = Place(entities, at);
        tank.chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 4000.0f});
        tank.chassis.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(1.7f, 0.5f, 3.2f)}});
        tank.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::TrackedVehicle{}});
        for (const float x : {-1.7f, 1.7f})
        {
            for (int i = 0; i < 9; ++i)
            {
                const float z = -2.8f + 0.7f * static_cast<float>(i);
                Fenrir::Entity wheel = entities.CreateEntity();
                wheel.AddComponent<Fenrir::VehicleWheel>(Fenrir::VehicleWheel{
                    .vehicle = tank.chassis,
                    .position = Vec3(x, -0.8f, z),
                    .track = x < 0.0f ? Fenrir::VehicleTrackSide::Left : Fenrir::VehicleTrackSide::Right,
                    .suspension = {.travel = 0.2f, .spring = {.frequency = 1.0f, .damping = 0.5f}}});
                tank.wheels.push_back(wheel);
            }
        }
        return tank;
    }

    Fenrir::Entity AddFloor(Fenrir::EntityList& entities, float friction = 0.5f, float size = 2000.0f)
    {
        Fenrir::Entity ground = Place(entities, Vec3(0.0f, -0.5f, 0.0f));
        ground.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(size, 0.5f, size)}});
        ground.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = friction});
        return ground;
    }

    // Game code: `drive` writes the input each tick, given the tick's index.
    void Drive(Fenrir::App& app, const Fenrir::Entity& chassis, std::function<void(int, Fenrir::Vehicle&)> drive)
    {
        auto tick = std::make_shared<int>(0);
        app.AddSequentialSystems(Fenrir::SchedulePriority::Tick, {[chassis, drive, tick](Fenrir::App&) {
                                     drive((*tick)++, chassis.GetComponent<Fenrir::Vehicle>());
                                 }});
    }

    float YawOf(const Fenrir::Entity& entity)
    {
        const Vec3 forward = Fenrir::GetWorldTransform(entity).rot * Vec3(0.0f, 0.0f, -1.0f);
        return std::atan2(-forward.x, -forward.z);
    }

    float Horizontal(const Vec3& v)
    {
        return std::sqrt(v.x * v.x + v.z * v.z);
    }
} // namespace

TEST_CASE("Spike: a default car accelerates, shifts up its geometric gears and stops", "[vehicle-spike]")
{
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    std::vector<int> gears;
    float time100 = -1.0f;
    Drive(physics.app, car.chassis, [&](int tick, Fenrir::Vehicle& vehicle) {
        vehicle.input.forward = tick > 60 && tick < 60 + 15 * 60 ? 1.0f : 0.0f;
        vehicle.input.brake = tick >= 60 + 15 * 60 ? 1.0f : 0.0f;
        if (gears.empty() || gears.back() != vehicle.state.gear)
        {
            gears.push_back(vehicle.state.gear);
            std::printf("  tick %d: gear %d at %.1f km/h, %.0f RPM\n", tick, vehicle.state.gear,
                        static_cast<double>(vehicle.state.forwardSpeed * 3.6f),
                        static_cast<double>(vehicle.state.engineRpm));
        }
        if (time100 < 0.0f && vehicle.state.forwardSpeed * 3.6f >= 100.0f)
        {
            time100 = static_cast<float>(tick - 60) / 60.0f;
        }
    });

    physics.Run(1.0);
    const Fenrir::VehicleState& state = car.chassis.GetComponent<Fenrir::Vehicle>().state;
    const Fenrir::VehicleWheel& front = car.wheels[0].GetComponent<Fenrir::VehicleWheel>();
    std::printf("Car settled: %d wheels on ground, front compression %.3f m, suspension force %.0f N, chassis y %.3f\n",
                state.wheelsOnGround, static_cast<double>(front.state.compression),
                static_cast<double>(front.state.suspensionForce), static_cast<double>(PositionOf(car.chassis).y));
    CHECK(state.wheelsOnGround == 4);

    physics.Run(15.0);
    std::printf("Car after 15 s full throttle: %.1f km/h in gear %d, 0-100 km/h in %.1f s\n",
                static_cast<double>(state.forwardSpeed * 3.6f), state.gear, static_cast<double>(time100));
    CHECK(state.forwardSpeed > 20.0f);
    CHECK(state.gear >= 4);

    // Each forward gear's ratio from the steady engine-to-wheel speed: geometric from 2.66 to 0.74.
    physics.Run(8.0);
    std::printf("Car after 8 s braking: %.2f km/h, sleeping %d\n", static_cast<double>(state.forwardSpeed * 3.6f),
                Fenrir::PhysicsWorld::Of(physics.app.GetActiveScene())->IsSleeping(car.chassis) ? 1 : 0);
    CHECK(std::abs(state.forwardSpeed) < 0.1f);
}

TEST_CASE("Spike: the gearbox's ratios are spaced geometrically from first to top", "[vehicle-spike]")
{
    // A car in Manual held in each gear: engine RPM over wheel RPM is the gear ratio times the differential's 3.42.
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    Fenrir::WheeledVehicle kind;
    kind.transmission.mode = Fenrir::TransmissionMode::Manual;
    car.chassis.GetComponent<Fenrir::Vehicle>().kind = kind;
    auto gear = std::make_shared<int>(1);
    Drive(physics.app, car.chassis, [gear](int, Fenrir::Vehicle& vehicle) {
        vehicle.input.forward = 0.3f;
        vehicle.input.gear = *gear;
    });
    for (int g = 1; g <= 5; ++g)
    {
        *gear = g;
        physics.Run(4.0);
        const Fenrir::VehicleState& state = car.chassis.GetComponent<Fenrir::Vehicle>().state;
        const float wheelRpm =
            car.wheels[2].GetComponent<Fenrir::VehicleWheel>().state.angularVelocity * 60.0f / (2.0f * 3.14159265f);
        const float expected = 2.66f * std::pow(0.74f / 2.66f, static_cast<float>(g - 1) / 4.0f);
        std::printf("  gear %d (engaged %d): engine %.0f RPM, wheel %.0f RPM, ratio %.3f, expected %.3f\n", g,
                    state.gear, static_cast<double>(state.engineRpm), static_cast<double>(wheelRpm),
                    static_cast<double>(state.engineRpm / wheelRpm / 3.42f), static_cast<double>(expected));
        CHECK(state.gear == g);
    }
}

TEST_CASE("Spike: a handbraked car on a slope stays put and falls asleep", "[vehicle-spike]")
{
    for (const float degrees : {10.0f, 20.0f})
    {
        PhysicsApp physics;
        const float slope = degrees * std::numbers::pi_v<float> / 180.0f;
        const Fenrir::Math::Quat tilt = Fenrir::Math::AngleAxis(slope, Vec3(1.0f, 0.0f, 0.0f));
        Fenrir::Entity ramp = Place(physics.Entities(), Vec3(0.0f, -0.5f, 0.0f));
        ramp.GetComponent<Fenrir::Transform>().rot = tilt;
        ramp.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(20.0f, 0.5f, 20.0f)}});
        Car car = AddCar(physics.Entities(), tilt * Vec3(0.0f, 1.0f, 0.0f), tilt);
        car.chassis.GetComponent<Fenrir::Vehicle>().input.handbrake = 1.0f;
        physics.Run(2.0);
        const Vec3 settled = PositionOf(car.chassis);
        int asleepAt = -1;
        for (int frame = 0; frame < 600 && asleepAt < 0; ++frame)
        {
            physics.app.RunFrame(tick);
            if (Fenrir::PhysicsWorld::Of(physics.app.GetActiveScene())->IsSleeping(car.chassis))
            {
                asleepAt = frame;
            }
        }
        physics.Run(5.0);
        const float crept = Fenrir::Math::Magnitude(PositionOf(car.chassis) - settled);
        std::printf("Handbraked on %.0f°: crept %.4f m in the 5+ s after settling, asleep after %d more ticks\n",
                    static_cast<double>(degrees), static_cast<double>(crept), asleepAt);
        CHECK(crept < 0.02f);
        CHECK(asleepAt >= 0);
    }
}

TEST_CASE("Spike: a car parked on the flat with no input does not creep", "[vehicle-spike]")
{
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    physics.Run(2.0);
    const Vec3 settled = PositionOf(car.chassis);
    physics.Run(10.0);
    const bool asleep = Fenrir::PhysicsWorld::Of(physics.app.GetActiveScene())->IsSleeping(car.chassis);
    std::printf("Parked on the flat, no input: moved %.5f m in 10 s, asleep %d\n",
                static_cast<double>(Fenrir::Math::Magnitude(PositionOf(car.chassis) - settled)), asleep ? 1 : 0);
    CHECK(asleep);
}

TEST_CASE("Spike: a motorcycle stands at rest and leans into turns", "[vehicle-spike]")
{
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car bike = AddMotorcycle(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    auto steer = std::make_shared<float>(0.0f);
    auto forward = std::make_shared<float>(0.0f);
    Drive(physics.app, bike.chassis, [steer, forward](int, Fenrir::Vehicle& vehicle) {
        vehicle.input.forward = *forward;
        vehicle.input.steer = *steer;
    });
    physics.Run(5.0);
    const Fenrir::VehicleState& state = bike.chassis.GetComponent<Fenrir::Vehicle>().state;
    std::printf("Motorcycle at rest 5 s: lean %.2f°, %d wheels on ground, y %.3f\n",
                static_cast<double>(state.leanAngle * 180.0f / 3.14159265f), state.wheelsOnGround,
                static_cast<double>(PositionOf(bike.chassis).y));
    CHECK(std::abs(state.leanAngle) < 0.1f);

    *forward = 1.0f;
    physics.Run(4.0);
    std::printf("Motorcycle after 4 s throttle: %.1f km/h, lean %.2f°\n",
                static_cast<double>(state.forwardSpeed * 3.6f),
                static_cast<double>(state.leanAngle * 180.0f / 3.14159265f));
    *forward = 0.3f;
    *steer = 1.0f;
    float most = 0.0f;
    const float yawBefore = YawOf(bike.chassis);
    for (int i = 0; i < 180; ++i)
    {
        physics.app.RunFrame(tick);
        most = std::max(most, state.leanAngle);
        if (i % 20 == 0)
        {
            std::printf(
                "    t %d: lean %.1f°, steer %.2f°, yaw %.2f, %.1f km/h\n", i,
                static_cast<double>(state.leanAngle * 57.3f),
                static_cast<double>(bike.wheels[0].GetComponent<Fenrir::VehicleWheel>().state.steerAngle * 57.3f),
                static_cast<double>(YawOf(bike.chassis) - yawBefore), static_cast<double>(state.forwardSpeed * 3.6f));
        }
    }
    std::printf("Motorcycle steering right 3 s: lean %.1f° (most %.1f°), %.1f km/h, %d wheels on ground\n",
                static_cast<double>(state.leanAngle * 180.0f / 3.14159265f),
                static_cast<double>(most * 180.0f / 3.14159265f), static_cast<double>(state.forwardSpeed * 3.6f),
                state.wheelsOnGround);
    CHECK(most > 0.1f);
    CHECK(state.wheelsOnGround == 2);
}

TEST_CASE("Spike: a motorcycle's maxAngle rebuild keeps RPM, gear and wheel spin", "[vehicle-spike]")
{
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car bike = AddMotorcycle(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    Drive(physics.app, bike.chassis, [](int, Fenrir::Vehicle& vehicle) { vehicle.input.forward = 1.0f; });
    physics.Run(6.0);
    Fenrir::Vehicle& vehicle = bike.chassis.GetComponent<Fenrir::Vehicle>();
    const Fenrir::VehicleState before = vehicle.state;
    const float spinBefore = bike.wheels[1].GetComponent<Fenrir::VehicleWheel>().state.angularVelocity;
    vehicle.kind.As<Fenrir::MotorcycleVehicle>().lean.maxAngle = 0.5f;
    physics.app.RunFrame(tick);
    const float spinAfter = bike.wheels[1].GetComponent<Fenrir::VehicleWheel>().state.angularVelocity;
    std::printf("maxAngle rebuild: RPM %.0f -> %.0f, gear %d -> %d, rear spin %.2f -> %.2f rad/s\n",
                static_cast<double>(before.engineRpm), static_cast<double>(vehicle.state.engineRpm), before.gear,
                vehicle.state.gear, static_cast<double>(spinBefore), static_cast<double>(spinAfter));
    CHECK(vehicle.state.gear == before.gear);
    CHECK(std::abs(vehicle.state.engineRpm - before.engineRpm) < 0.1f * before.engineRpm);
    CHECK(std::abs(spinAfter - spinBefore) < 0.1f * std::abs(spinBefore));
}

TEST_CASE("Spike: a tank turns on the spot", "[vehicle-spike]")
{
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car tank = AddTank(physics.Entities(), Vec3(0.0f, 1.5f, 0.0f));
    auto pivot = std::make_shared<bool>(false);
    Drive(physics.app, tank.chassis, [pivot](int, Fenrir::Vehicle& vehicle) {
        vehicle.input.forward = *pivot ? 1.0f : 0.0f;
        vehicle.input.leftTrack = *pivot ? -1.0f : 1.0f;
        vehicle.input.rightTrack = 1.0f;
    });
    physics.Run(2.0);
    const Fenrir::VehicleState& state = tank.chassis.GetComponent<Fenrir::Vehicle>().state;
    std::printf("Tank settled: %d wheels on ground, y %.3f\n", state.wheelsOnGround,
                static_cast<double>(PositionOf(tank.chassis).y));
    const Vec3 start = PositionOf(tank.chassis);
    const float yaw = YawOf(tank.chassis);
    *pivot = true;
    float turned = 0.0f;
    float last = yaw;
    for (int i = 0; i < 300; ++i)
    {
        physics.app.RunFrame(tick);
        float now = YawOf(tank.chassis);
        float step = now - last;
        step -= 2.0f * std::numbers::pi_v<float> * std::round(step / (2.0f * std::numbers::pi_v<float>));
        turned += step;
        last = now;
    }
    const float drift = Horizontal(PositionOf(tank.chassis) - start);
    std::printf("Tank pivot 5 s (left -1, right 1): turned %.0f°, drifted %.2f m, tracks %.2f / %.2f m/s, gear %d\n",
                static_cast<double>(turned * 180.0f / 3.14159265f), static_cast<double>(drift),
                static_cast<double>(state.leftTrackSpeed), static_cast<double>(state.rightTrackSpeed), state.gear);
    CHECK(std::abs(turned) > 1.0f);
    CHECK(drift < 1.0f);
}

TEST_CASE("Spike: wheel settings patched in place on a live vehicle take effect", "[vehicle-spike]")
{
    PhysicsApp physics;
    AddFloor(physics.Entities());
    Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    physics.Run(3.0);
    const float stiffY = PositionOf(car.chassis).y;
    // Softer springs sag further; a bigger radius lifts the chassis.
    for (Fenrir::Entity& wheel : car.wheels)
    {
        wheel.GetComponent<Fenrir::VehicleWheel>().suspension.spring.frequency = 0.8f;
    }
    physics.Run(3.0);
    const float softY = PositionOf(car.chassis).y;
    for (Fenrir::Entity& wheel : car.wheels)
    {
        wheel.GetComponent<Fenrir::VehicleWheel>().radius = 0.45f;
    }
    physics.Run(3.0);
    const float bigY = PositionOf(car.chassis).y;
    std::printf("Live wheel edits: chassis y %.3f (1.5 Hz), %.3f (0.8 Hz), %.3f (radius 0.45)\n",
                static_cast<double>(stiffY), static_cast<double>(softY), static_cast<double>(bigY));
    CHECK(softY < stiffY - 0.02f);
    CHECK(bigY > softY + 0.1f);

    // Steering only once a wheel is given an angle.
    for (Fenrir::Entity& wheel : car.wheels)
    {
        wheel.GetComponent<Fenrir::VehicleWheel>().maxSteerAngle = 0.0f;
    }
    Drive(physics.app, car.chassis, [](int, Fenrir::Vehicle& vehicle) {
        vehicle.input.forward = 0.5f;
        vehicle.input.steer = 1.0f;
    });
    physics.Run(3.0);
    const float straightYaw = YawOf(car.chassis);
    car.wheels[0].GetComponent<Fenrir::VehicleWheel>().maxSteerAngle = 0.5f;
    car.wheels[1].GetComponent<Fenrir::VehicleWheel>().maxSteerAngle = 0.5f;
    physics.Run(2.0);
    std::printf("Steer with maxSteerAngle 0: yaw %.3f rad after 3 s; with 0.5 on the front: yaw %.3f after 2 s more\n",
                static_cast<double>(straightYaw), static_cast<double>(YawOf(car.chassis)));
    CHECK(std::abs(straightYaw) < 0.02f);
    CHECK(YawOf(car.chassis) < -0.2f); // steering right turns clockwise seen from above: yaw goes negative
}

TEST_CASE("Spike: grip on ice, tarmac and grass under the geometric mean", "[vehicle-spike]")
{
    for (const float friction : {0.05f, 0.3f, 0.5f, 1.0f})
    {
        PhysicsApp physics;
        AddFloor(physics.Entities(), friction);
        Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
        Drive(physics.app, car.chassis, [](int tick, Fenrir::Vehicle& vehicle) {
            vehicle.input.forward = tick > 60 && tick <= 60 + 5 * 60 ? 1.0f : 0.0f;
            vehicle.input.brake = tick > 60 + 5 * 60 ? 1.0f : 0.0f;
        });
        physics.Run(1.0 + 5.0);
        const Fenrir::VehicleState& state = car.chassis.GetComponent<Fenrir::Vehicle>().state;
        const float speed = state.forwardSpeed;
        const float slip = car.wheels[2].GetComponent<Fenrir::VehicleWheel>().state.longitudinalSlip;
        const Vec3 brakeFrom = PositionOf(car.chassis);
        physics.Run(15.0);
        std::printf("Ground friction %.2f (combined peak %.2f): %.1f km/h after 5 s (rear slip %.2f), braking distance "
                    "%.1f m\n",
                    static_cast<double>(friction), static_cast<double>(std::sqrt(1.2f * friction)),
                    static_cast<double>(speed * 3.6f), static_cast<double>(slip),
                    static_cast<double>(Horizontal(PositionOf(car.chassis) - brakeFrom)));
    }
}

namespace
{
    // A drive over bumps and a turn, the chassis and every wheel's pose after each tick.
    std::vector<Fenrir::Transform> DriveRecorded(bool threads)
    {
        PhysicsApp physics(StepPosesOnly(), threads ? 4 : 0);
        AddFloor(physics.Entities());
        for (int i = 0; i < 10; ++i)
        {
            Fenrir::Entity bump = Place(physics.Entities(), Vec3(0.0f, 0.0f, -10.0f - 3.0f * static_cast<float>(i)));
            bump.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.785f, Vec3(1.0f, 0.0f, 0.0f));
            bump.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(3.0f, 0.1f, 0.1f)}});
        }
        for (int i = 0; i < 6; ++i)
        {
            AddBody(physics.Entities(), Vec3(1.0f, 0.5f, -45.0f - 2.0f * static_cast<float>(i)));
        }
        Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
        Car bike = AddMotorcycle(physics.Entities(), Vec3(6.0f, 1.0f, 0.0f));
        Car tank = AddTank(physics.Entities(), Vec3(-8.0f, 1.5f, 0.0f));
        Drive(physics.app, car.chassis, [](int tick, Fenrir::Vehicle& vehicle) {
            vehicle.input.forward = 1.0f;
            vehicle.input.steer = tick > 300 ? 0.6f : 0.0f;
        });
        Drive(physics.app, bike.chassis, [](int tick, Fenrir::Vehicle& vehicle) {
            vehicle.input.forward = 0.6f;
            vehicle.input.steer = tick > 200 ? -0.5f : 0.0f;
        });
        Drive(physics.app, tank.chassis, [](int tick, Fenrir::Vehicle& vehicle) {
            vehicle.input.forward = 1.0f;
            vehicle.input.leftTrack = tick > 200 ? 0.5f : 1.0f;
        });
        std::vector<Fenrir::Transform> poses;
        for (int i = 0; i < 600; ++i)
        {
            physics.app.RunFrame(tick);
            for (const Car* each : {&car, &bike, &tank})
            {
                poses.push_back(Fenrir::GetWorldTransform(each->chassis));
                for (const Fenrir::Entity& wheel : each->wheels)
                {
                    poses.push_back(Fenrir::GetWorldTransform(wheel));
                }
            }
        }
        return poses;
    }

    bool Identical(const std::vector<Fenrir::Transform>& a, const std::vector<Fenrir::Transform>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (std::memcmp(&a[i].pos, &b[i].pos, sizeof(a[i].pos)) != 0 ||
                std::memcmp(&a[i].rot, &b[i].rot, sizeof(a[i].rot)) != 0)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

TEST_CASE("Spike: two drives of a car, a motorcycle and a tank are bit-identical", "[vehicle-spike]")
{
    const std::vector<Fenrir::Transform> first = DriveRecorded(true);
    const std::vector<Fenrir::Transform> second = DriveRecorded(true);
    const std::vector<Fenrir::Transform> single = DriveRecorded(false);
    std::printf("Determinism: 600 ticks, %zu poses; two threaded runs identical %d, threaded vs single-threaded %d\n",
                first.size(), Identical(first, second) ? 1 : 0, Identical(first, single) ? 1 : 0);
    CHECK(Identical(first, second));
}

TEST_CASE("Spike: wheel poses blend between steps", "[vehicle-spike]")
{
    PhysicsApp physics{Fenrir::PhysicsConfig{}};
    AddFloor(physics.Entities());
    Car car = AddCar(physics.Entities(), Vec3(0.0f, 1.0f, 0.0f));
    Drive(physics.app, car.chassis, [](int, Fenrir::Vehicle& vehicle) {
        vehicle.input.forward = 1.0f;
        vehicle.input.steer = 0.5f;
    });
    physics.Run(3.0);
    // Frames of 0.3 ticks: the front wheel's shown position moves every frame, ticks or not.
    int moved = 0;
    int frames = 0;
    Vec3 last = PositionOf(car.wheels[0]);
    for (int i = 0; i < 30; ++i)
    {
        physics.app.RunFrame(shortFrame);
        const Vec3 now = PositionOf(car.wheels[0]);
        moved += Fenrir::Math::Magnitude(now - last) > 1e-5f ? 1 : 0;
        ++frames;
        last = now;
    }
    // The wheel stays on its axle relative to the shown chassis: within its travel of where it hangs.
    const Fenrir::Transform chassis = Fenrir::GetWorldTransform(car.chassis);
    const Vec3 local = Fenrir::Math::Conjugate(chassis.rot) * (PositionOf(car.wheels[0]) - chassis.pos);
    std::printf("Blending: front wheel moved in %d of %d short frames; chassis-local (%.3f, %.3f, %.3f)\n", moved,
                frames, static_cast<double>(local.x), static_cast<double>(local.y), static_cast<double>(local.z));
    CHECK(moved == frames);
    CHECK(std::abs(local.x + 0.9f) < 0.01f);
    CHECK(std::abs(local.z + 1.4f) < 0.01f);
}
