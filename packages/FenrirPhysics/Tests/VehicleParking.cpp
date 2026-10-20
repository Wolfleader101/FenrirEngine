// Parking, waking and wheel interpolation through the frame seam: a parked vehicle sleeps and keeps its state, any
// input or edit to a vehicle component wakes it, and between ticks its wheels are shown with its chassis.

#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float poseMargin = 1e-4f;
    constexpr float degrees = std::numbers::pi_v<float> / 180.0f;

    const Math::Vec3 axisX(1.0f, 0.0f, 0.0f);
    const Math::Vec3 axisY(0.0f, 1.0f, 0.0f);

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    // Runs ticks until the chassis sleeps, for at most 20 s; whether it does.
    bool RunUntilAsleep(PhysicsApp& test, const Entity& chassis)
    {
        for (int frame = 0; frame < 20 * 60; ++frame)
        {
            test.app.RunFrame(tick);
            if (World(test).IsSleeping(chassis))
            {
                return true;
            }
        }
        return false;
    }

    // A wheel on the chassis's roof, where it touches nothing.
    Entity AddRoofWheel(EntityList& entities, const Entity& chassis)
    {
        const Math::Vec3 position(0.0f, 0.6f, 0.0f);
        Entity wheel = Place(entities, GetWorldTransform(chassis).pos + position);
        wheel.AddComponent<VehicleWheel>(VehicleWheel{.vehicle = chassis, .position = position});
        return wheel;
    }

    bool BitIdentical(const Transform& a, const Transform& b)
    {
        return std::memcmp(&a.pos, &b.pos, sizeof(Math::Vec3)) == 0 &&
               std::memcmp(&a.rot, &b.rot, sizeof(Math::Quat)) == 0;
    }

    // Square bars 0.14 m high across the floor every 3 m ahead of the start, as the demo's bumps.
    void AddBumps(EntityList& entities)
    {
        for (int i = 0; i < 8; ++i)
        {
            Entity bump = Place(entities, Math::Vec3(0.0f, 0.0f, -6.0f - 3.0f * static_cast<float>(i)));
            bump.GetComponent<Transform>().rot = Math::AngleAxis(0.25f * std::numbers::pi_v<float>, axisX);
            bump.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(4.0f, 0.1f, 0.1f)}});
        }
    }

    // A wheel's compression, steer and spin speed after each step, by wheel.
    struct WheelSteps
    {
        std::vector<std::vector<VehicleWheelState>> states;
        std::vector<std::vector<Transform>> poses;
    };

    // Records each wheel's state and world pose after each step: after physics's PostTick system.
    std::shared_ptr<WheelSteps> RecordWheelSteps(App& app, const Car& car)
    {
        auto steps = std::make_shared<WheelSteps>();
        steps->states.resize(car.wheels.size());
        steps->poses.resize(car.wheels.size());
        app.AddSequentialSystems(SchedulePriority::PostTick, {[steps, car](App&) {
                                     for (std::size_t i = 0; i < car.wheels.size(); ++i)
                                     {
                                         steps->states[i].push_back(StateOf(car.wheels[i]));
                                         steps->poses[i].push_back(GetWorldTransform(car.wheels[i]));
                                     }
                                 }});
        return steps;
    }

    // The car driven slowly over the bumps, weaving: compression, steer and spin all change from step to step.
    void DriveOverBumps(App& app, const Car& car)
    {
        Drive(app, car.chassis, [](int ticks, Vehicle& vehicle) {
            vehicle.input.forward = 0.2f;
            vehicle.input.steer = 0.6f * std::sin(0.05f * static_cast<float>(ticks));
        });
    }

    // The spin about its axle a wheel is shown at, from its rotation relative to the chassis's and its steer: radians,
    // + rolling forward, wrapped to a turn.
    float ShownSpin(const Transform& chassis, const Transform& wheel, float steer)
    {
        const Math::Quat unsteered = Math::AngleAxis(steer, axisY) * Math::Conjugate(chassis.rot) * wheel.rot;
        const Math::Vec3 up = unsteered * axisY;
        return std::atan2(-up.z, up.y);
    }
    // Asleep, a vehicle keeps the state it fell asleep with for a second, its wheels still and where they were.
    void RequireKeptWhileAsleep(PhysicsApp& test, const Car& car)
    {
        const VehicleState parked = StateOf(car);
        std::vector<VehicleWheelState> wheels;
        std::vector<Transform> poses;
        for (const Entity& wheel : car.wheels)
        {
            INFO("asleep, each wheel's spin reads 0");
            REQUIRE(Exactly(StateOf(wheel).angularVelocity, 0.0f));
            wheels.push_back(StateOf(wheel));
            poses.push_back(GetWorldTransform(wheel));
        }
        test.Run(1.0);
        REQUIRE(World(test).IsSleeping(car.chassis));
        INFO("a second later it keeps the state it fell asleep with");
        REQUIRE(Exactly(StateOf(car).forwardSpeed, parked.forwardSpeed));
        REQUIRE(Exactly(StateOf(car).engineRpm, parked.engineRpm));
        REQUIRE(StateOf(car).gear == parked.gear);
        REQUIRE(StateOf(car).wheelsOnGround == parked.wheelsOnGround);
        for (std::size_t i = 0; i < car.wheels.size(); ++i)
        {
            const VehicleWheelState& now = StateOf(car.wheels[i]);
            INFO("wheel " << i << " keeps its state, still, and its pose");
            REQUIRE(now.onGround == wheels[i].onGround);
            REQUIRE(Exactly(now.compression, wheels[i].compression));
            REQUIRE(Exactly(now.suspensionForce, wheels[i].suspensionForce));
            REQUIRE(Exactly(now.longitudinalSlip, wheels[i].longitudinalSlip));
            REQUIRE(Exactly(now.lateralSlip, wheels[i].lateralSlip));
            REQUIRE(Exactly(now.steerAngle, wheels[i].steerAngle));
            REQUIRE(Exactly(now.angularVelocity, 0.0f));
            REQUIRE(BitIdentical(GetWorldTransform(car.wheels[i]), poses[i]));
        }
    }
} // namespace

TEST_CASE("Driver input, an edit to each vehicle component and a rebuild wake a sleeping chassis",
          "[physics][vehicles][parking]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Entity differential = Place(test.Entities(), Math::Vec3(0.0f));
    differential.AddComponent<VehicleDifferential>(
        VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3]});
    Entity bar = Place(test.Entities(), Math::Vec3(0.0f));
    bar.AddComponent<VehicleAntiRollBar>(VehicleAntiRollBar{.leftWheel = car.wheels[0], .rightWheel = car.wheels[1]});
    INFO("left alone, the parked car falls asleep");
    REQUIRE(RunUntilAsleep(test, car.chassis));

    Vehicle& vehicle = car.chassis.GetComponent<Vehicle>();
    SECTION("with nothing changed it sleeps on")
    {
        test.Run(1.0);
        REQUIRE(World(test).IsSleeping(car.chassis));
        return;
    }
    SECTION("the handbrake pulled")
    {
        vehicle.input.handbrake = 1.0f;
    }
    SECTION("a gear chosen")
    {
        vehicle.input.gear = 1;
    }
    SECTION("an input its range clamps away")
    {
        vehicle.input.brake = -1.0f;
    }
    SECTION("the engine's torque edited")
    {
        vehicle.kind.As<WheeledVehicle>().engine.maxTorque = 300.0f;
    }
    SECTION("the probe changed")
    {
        vehicle.probe = VehicleProbe::Ray;
    }
    SECTION("the tilt limit changed")
    {
        vehicle.maxTilt = 1.0f;
    }
    SECTION("a wheel's spring edited")
    {
        car.wheels[0].GetComponent<VehicleWheel>().suspension.spring.frequency = 2.0f;
    }
    SECTION("the differential's ratio edited")
    {
        differential.GetComponent<VehicleDifferential>().ratio = 4.0f;
    }
    SECTION("the anti-roll bar's stiffness edited")
    {
        bar.GetComponent<VehicleAntiRollBar>().stiffness = 500.0f;
    }
    SECTION("a wheel added, a rebuild")
    {
        AddRoofWheel(test.Entities(), car.chassis);
    }
    test.app.RunFrame(tick);
    INFO("the tick after, the chassis is awake");
    REQUIRE_FALSE(World(test).IsSleeping(car.chassis));
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A handbraked car on a 10 degree slope creeps under 1 mm and sleeps, keeping its state with its wheels still",
          "[physics][vehicles][parking]")
{
    PhysicsApp test;
    const Math::Quat slope = Math::AngleAxis(10.0f * degrees, axisX);
    const Math::Vec3 up = slope * axisY;
    // Its top through the origin, rising to the north (-Z).
    Entity floor = AddFloor(test.Entities(), 0.5f, 200.0f);
    floor.GetComponent<Transform>() = Transform(-0.5f * up, slope, Math::Vec3(1.0f));
    Car car = AddCar(test.Entities(), up, slope);
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.handbrake = 1.0f; });
    test.Run(2.0);

    const Math::Vec3 downhill = slope * Math::Vec3(0.0f, 0.0f, 1.0f);
    const Math::Vec3 settled = PositionOf(car.chassis);
    INFO("settled on its wheels, it falls asleep");
    REQUIRE(RunUntilAsleep(test, car.chassis));
    const float creep = Math::Dot(PositionOf(car.chassis) - settled, downhill);
    INFO("it crept " << creep * 1000.0f << " mm down the slope");
    REQUIRE(std::abs(creep) < 1e-3f);
    REQUIRE(StateOf(car).wheelsOnGround == 4);

    RequireKeptWhileAsleep(test, car);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A car left to roll to rest on the flat sleeps with its wheels stopped, keeping its state",
          "[physics][vehicles][parking]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    // Driven off, braked almost to rest, then let go: it rolls on slowly, its wheels turning, until it sleeps.
    Drive(test.app, car.chassis, [](int ticks, Vehicle& vehicle) {
        vehicle.input.forward = ticks < 60 ? 0.2f : 0.0f;
        vehicle.input.brake = ticks >= 60 && vehicle.state.forwardSpeed > 0.02f ? 1.0f : 0.0f;
    });
    test.Run(1.0);
    REQUIRE(StateOf(car).forwardSpeed > 0.5f);
    INFO("it rolls to rest and falls asleep");
    REQUIRE(RunUntilAsleep(test, car.chassis));
    RequireKeptWhileAsleep(test, car);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Between ticks each wheel is shown on its axle on the blended chassis, blended in the chassis's axes",
          "[physics][vehicles][parking][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddFloor(test.Entities());
    AddBumps(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    DriveOverBumps(test.app, car);
    const std::shared_ptr<WheelSteps> steps = RecordWheelSteps(test.app, car);
    while (steps->states[0].size() < 60)
    {
        test.app.RunFrame(shortFrame);
    }

    std::vector<float> lastSpin(car.wheels.size(), 0.0f);
    int blended = 0;
    int spinning = 0;
    for (int frame = 0; steps->states[0].size() < 360; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const float fraction = FractionOf(test.app);
        const Transform chassis = GetWorldTransform(car.chassis);
        blended += fraction > 0.05f ? 1 : 0;
        for (std::size_t i = 0; i < car.wheels.size(); ++i)
        {
            const VehicleWheel& settings = car.wheels[i].GetComponent<VehicleWheel>();
            const std::vector<VehicleWheelState>& states = steps->states[i];
            const VehicleWheelState& from = states[states.size() - 2];
            const VehicleWheelState& to = states.back();
            const float compression = from.compression + (to.compression - from.compression) * fraction;
            const float steer = from.steerAngle + (to.steerAngle - from.steerAngle) * fraction;
            const Transform wheel = GetWorldTransform(car.wheels[i]);
            const Math::Vec3 local = Math::Conjugate(chassis.rot) * (wheel.pos - chassis.pos);
            INFO("frame " << frame << ", wheel " << i << ", " << fraction << " of a tick on: at (" << local.x << ", "
                          << local.y << ", " << local.z << ") in the chassis's axes, its compression blended to "
                          << compression);
            REQUIRE(local.x == Approx(settings.position.x).margin(poseMargin));
            REQUIRE(local.y == Approx(settings.position.y + compression).margin(poseMargin));
            REQUIRE(local.z == Approx(settings.position.z).margin(poseMargin));
            const Math::Vec3 axle = Math::Conjugate(chassis.rot) * (wheel.rot * axisX);
            INFO("its axle steered by its blended steer, " << steer);
            REQUIRE(axle.x == Approx(std::cos(steer)).margin(poseMargin));
            REQUIRE(axle.y == Approx(0.0f).margin(poseMargin));
            REQUIRE(axle.z == Approx(std::sin(steer)).margin(poseMargin));

            // Rolling forward, the spin shown only ever turns forward, across the backend's wrap too.
            const float spin = ShownSpin(chassis, wheel, steer);
            if (frame > 0 && from.angularVelocity > 1.0f && to.angularVelocity > 1.0f)
            {
                const float turned = std::remainder(spin - lastSpin[i], 2.0f * std::numbers::pi_v<float>);
                INFO("its spin turned " << turned << " rad since the last frame");
                REQUIRE(turned > 0.0f);
                ++spinning;
            }
            lastSpin[i] = spin;
        }
    }
    INFO("most frames end part of the way through a tick, with the wheels rolling");
    REQUIRE(blended > 500);
    REQUIRE(spinning > 2000);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Tick systems see each wheel's true pose: BeforeTicks puts back the step pose",
          "[physics][vehicles][parking][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddFloor(test.Entities());
    AddBumps(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    DriveOverBumps(test.app, car);
    const std::shared_ptr<WheelSteps> steps = RecordWheelSteps(test.app, car);
    auto seen = std::make_shared<int>(0);
    auto wrong = std::make_shared<int>(0);
    test.app.AddSequentialSystems(
        SchedulePriority::Tick, {[steps, car, seen, wrong](App&) {
            for (std::size_t i = 0; i < car.wheels.size() && !steps->poses[i].empty(); ++i)
            {
                ++*seen;
                *wrong += BitIdentical(GetWorldTransform(car.wheels[i]), steps->poses[i].back()) ? 0 : 1;
            }
        }});
    while (steps->states[0].size() < 300)
    {
        test.app.RunFrame(shortFrame);
    }
    INFO(*wrong << " of " << *seen << " wheel poses a Tick system saw were not the last step's, bit for bit");
    REQUIRE(*seen > 1000);
    REQUIRE(*wrong == 0);
}

TEST_CASE("With its chassis not interpolated a vehicle's wheels are shown at their step poses",
          "[physics][vehicles][parking][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddFloor(test.Entities());
    AddBumps(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    car.chassis.GetComponent<RigidBody>().interpolate = false;
    DriveOverBumps(test.app, car);
    const std::shared_ptr<WheelSteps> steps = RecordWheelSteps(test.app, car);
    int frames = 0;
    while (steps->states[0].size() < 300)
    {
        test.app.RunFrame(shortFrame);
        for (std::size_t i = 0; i < car.wheels.size() && !steps->poses[i].empty(); ++i)
        {
            INFO("frame " << frames << ": wheel " << i << " is at its last step pose");
            REQUIRE(BitIdentical(GetWorldTransform(car.wheels[i]), steps->poses[i].back()));
        }
        ++frames;
    }
    REQUIRE(frames > 900);
}

TEST_CASE("A wheel entity destroyed between frames is left out of the blend until its vehicle is rebuilt",
          "[physics][vehicles][parking][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    DriveOverBumps(test.app, car);
    test.Run(1.0);

    // Destroyed in Update; the frames after it end before the next tick, so the blend runs before the drain.
    test.Entities().DestroyEntity(car.wheels[2]);
    test.app.RunFrame(tick * 0.1);
    test.app.RunFrame(tick * 0.1);
    for (int frame = 0; frame < 20; ++frame)
    {
        test.app.RunFrame(shortFrame);
    }
    const Transform chassis = GetWorldTransform(car.chassis);
    const Math::Vec3 local = Math::Conjugate(chassis.rot) * (GetWorldTransform(car.wheels[3]).pos - chassis.pos);
    INFO("the rebuilt car's rear-right wheel is still blended on its axle, at x = " << local.x);
    REQUIRE(local.x == Approx(wheelX).margin(poseMargin));
    REQUIRE(local.z == Approx(wheelZ).margin(poseMargin));
}
