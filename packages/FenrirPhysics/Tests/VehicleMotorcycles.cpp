// Motorcycles through the frame seam (vehicles spec, kinds: MotorcycleVehicle and MotorcycleLean): Jolt's sample bike
// standing at rest, leaning into a turn, its steering capped by speed, falling over with its lean controller off,
// landing from a jump without diverging, and rebuilt by a lean-limit or kind change carrying over its engine speed,
// gear and wheel spin. Its debug lines and wheel interpolation are a car's.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/DebugLines.hpp"
#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float degree = std::numbers::pi_v<float> / 180.0f;
    const Math::Vec3 axisX(1.0f, 0.0f, 0.0f);
    const Math::Vec3 axisZ(0.0f, 0.0f, 1.0f);
    // How far one tick may move a carried engine speed or wheel spin: a fresh one would be off by far more.
    constexpr float carried = 0.03f;

    Vehicle& VehicleOf(const Car& bike)
    {
        return bike.chassis.GetComponent<Vehicle>();
    }

    MotorcycleLean& LeanOf(const Car& bike)
    {
        return VehicleOf(bike).kind.As<MotorcycleVehicle>().lean;
    }

    float AngularSpeedOf(const Entity& chassis)
    {
        return Math::Magnitude(chassis.GetComponent<RigidBody>().angularVelocity);
    }

    // Radians the entity's right (+X) has dipped below the horizontal: + right, as VehicleState::leanAngle.
    float LeanOf(const Transform& at)
    {
        const Math::Vec3 right = at.rot * axisX;
        return -std::asin(std::clamp(right.y, -1.0f, 1.0f));
    }

    // Game code holding `speed` with the throttle and brake, steering `steer`.
    void Hold(Vehicle& vehicle, float speed, float steer)
    {
        const float error = speed - vehicle.state.forwardSpeed;
        vehicle.input.forward = std::clamp(0.4f + error, 0.0f, 1.0f);
        vehicle.input.brake = std::clamp(-error, 0.0f, 1.0f);
        vehicle.input.steer = steer;
    }

    // The bike on the floor, settled for a second, then driven up to 60 km/h in a straight line over 15 s.
    Car BikeAtSixty(PhysicsApp& test, float maxAngle = MotorcycleLean{}.maxAngle, bool limitSteering = true)
    {
        AddFloor(test.Entities());
        Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        LeanOf(bike).maxAngle = maxAngle;
        LeanOf(bike).limitSteering = limitSteering;
        test.Run(1.0);
        for (int i = 0; i < 900; ++i)
        {
            Hold(VehicleOf(bike), 60.0f * kmh, 0.0f);
            test.Run(tick);
        }
        INFO("at speed: " << StateOf(bike).forwardSpeed / kmh << " km/h");
        REQUIRE(StateOf(bike).forwardSpeed == Approx(60.0f * kmh).margin(3.0f * kmh));
        return bike;
    }

    // The lean and front wheel steer over the last 3 s of 5 s holding 60 km/h under full steer: the least, mean and
    // most lean, and the most steer.
    struct Turn
    {
        float leastLean = 10.0f;
        float meanLean = 0.0f;
        float mostLean = -10.0f;
        float mostSteer = 0.0f;
    };

    Turn TurnAtSixty(PhysicsApp& test, const Car& bike)
    {
        Turn turn;
        int counted = 0;
        for (int i = 0; i < 300; ++i)
        {
            Hold(VehicleOf(bike), 60.0f * kmh, 1.0f);
            test.Run(tick);
            if (i >= 120)
            {
                const float lean = StateOf(bike).leanAngle;
                turn.leastLean = std::min(turn.leastLean, lean);
                turn.mostLean = std::max(turn.mostLean, lean);
                turn.meanLean += lean;
                turn.mostSteer = std::max(turn.mostSteer, StateOf(bike.wheels[0]).steerAngle);
                ++counted;
            }
        }
        turn.meanLean /= static_cast<float>(counted);
        return turn;
    }

    // What a rebuild should carry over, read after the tick before it.
    struct Before
    {
        float engineRpm;
        int gear;
        std::vector<float> spins;
    };

    Before Read(const Car& vehicle)
    {
        Before before{.engineRpm = StateOf(vehicle).engineRpm, .gear = StateOf(vehicle).gear, .spins = {}};
        for (const Entity& wheel : vehicle.wheels)
        {
            before.spins.push_back(StateOf(wheel).angularVelocity);
        }
        return before;
    }

    void RequireCarried(const Car& vehicle, const Before& before)
    {
        const VehicleState& state = StateOf(vehicle);
        INFO("the engine speed carries over (" << before.engineRpm << " to " << state.engineRpm << " RPM)");
        REQUIRE(state.engineRpm == Approx(before.engineRpm).epsilon(carried));
        INFO("so does the gear");
        REQUIRE(state.gear == before.gear);
        for (std::size_t i = 0; i < vehicle.wheels.size(); ++i)
        {
            const float spin = StateOf(vehicle.wheels[i]).angularVelocity;
            INFO("wheel " << i << "'s spin carries over (" << before.spins[i] << " to " << spin << " rad/s)");
            REQUIRE(spin == Approx(before.spins[i]).epsilon(carried));
        }
    }

    // The bike driven gently in a straight line for two and a half seconds, its engine above idle in first gear.
    Car DrivenBike(PhysicsApp& test)
    {
        AddFloor(test.Entities());
        Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        VehicleOf(bike).input.forward = 0.4f;
        test.Run(2.5);
        INFO("driven: " << StateOf(bike).engineRpm << " RPM in gear " << StateOf(bike).gear);
        REQUIRE(StateOf(bike).gear >= 1);
        REQUIRE(StateOf(bike).engineRpm > 1200.0f);
        REQUIRE(StateOf(bike.wheels[1]).angularVelocity > 5.0f);
        return bike;
    }
} // namespace

TEST_CASE("The default motorcycle stands at rest on its two wheels, and other kinds read no lean",
          "[physics][vehicles][motorcycles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Car car = AddCar(test.Entities(), Math::Vec3(10.0f, 1.0f, 0.0f));
    test.Run(5.0);

    INFO("upright (lean " << StateOf(bike).leanAngle / degree << "°), still, on both wheels");
    REQUIRE(std::abs(StateOf(bike).leanAngle) < 1.0f * degree);
    REQUIRE(std::abs(LeanOf(GetWorldTransform(bike.chassis))) < 1.0f * degree);
    REQUIRE(AngularSpeedOf(bike.chassis) < 0.01f);
    REQUIRE(StateOf(bike).wheelsOnGround == 2);
    INFO("a car's lean is 0");
    REQUIRE(Exactly(StateOf(car).leanAngle, 0.0f));
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A motorcycle under full steer at 60 km/h leans about 23 degrees into the turn",
          "[physics][vehicles][motorcycles]")
{
    PhysicsApp test;
    const Car bike = BikeAtSixty(test);
    const Turn turn = TurnAtSixty(test, bike);

    INFO("leaning right, steady, from " << turn.leastLean / degree << "° to " << turn.mostLean / degree << "° (mean "
                                        << turn.meanLean / degree << "°)");
    REQUIRE(turn.meanLean == Approx(23.0f * degree).margin(3.0f * degree));
    REQUIRE(turn.mostLean - turn.leastLean < 3.0f * degree);
    INFO("turning right (clockwise from above), still on both wheels");
    REQUIRE(bike.chassis.GetComponent<RigidBody>().angularVelocity.y < -0.1f);
    REQUIRE(StateOf(bike).wheelsOnGround == 2);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("limitSteering caps a motorcycle's steer by speed, so its lean stays under maxAngle",
          "[physics][vehicles][motorcycles]")
{
    constexpr float maxLean = 15.0f * degree;
    SECTION("on, at 60 km/h the fork turns only a few degrees, and the lean stays under a 15 degree maxAngle")
    {
        PhysicsApp test;
        const Car bike = BikeAtSixty(test, maxLean);
        const Turn turn = TurnAtSixty(test, bike);
        INFO("most steer " << turn.mostSteer / degree << "°, most lean " << turn.mostLean / degree << "°");
        REQUIRE(turn.mostSteer < 0.25f * forkAngle);
        REQUIRE(turn.mostLean < maxLean + 0.5f * degree);
        REQUIRE(turn.meanLean > 0.5f * maxLean);
        REQUIRE(test.warnings.empty());
    }
    SECTION("off, the fork turns its full 30 degrees")
    {
        PhysicsApp test;
        const Car bike = BikeAtSixty(test, maxLean, false);
        const Turn turn = TurnAtSixty(test, bike);
        INFO("most steer " << turn.mostSteer / degree << "°");
        REQUIRE(turn.mostSteer == Approx(forkAngle).margin(1e-3f));
        REQUIRE(test.warnings.empty());
    }
    SECTION("a live edit: switched off mid-turn, the fork turns its full 30 degrees from the next tick")
    {
        PhysicsApp test;
        const Car bike = BikeAtSixty(test, maxLean);
        Hold(VehicleOf(bike), 60.0f * kmh, 1.0f);
        test.Run(1.0);
        REQUIRE(StateOf(bike.wheels[0]).steerAngle < 0.25f * forkAngle);
        LeanOf(bike).limitSteering = false;
        test.Run(tick);
        test.Run(tick);
        INFO("steer " << StateOf(bike.wheels[0]).steerAngle / degree << "°");
        REQUIRE(StateOf(bike.wheels[0]).steerAngle == Approx(forkAngle).margin(1e-3f));
        REQUIRE(test.warnings.empty());
    }
}

TEST_CASE("A motorcycle with lean.enabled turned off falls over, a live edit", "[physics][vehicles][motorcycles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    // Tipped 3° to the left: upright and perfectly balanced, it would stand.
    Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), Math::AngleAxis(3.0f * degree, axisZ));
    test.Run(3.0);
    INFO("with its lean controller on, it rights itself (lean " << StateOf(bike).leanAngle / degree << "°)");
    REQUIRE(std::abs(StateOf(bike).leanAngle) < 2.0f * degree);
    REQUIRE(StateOf(bike).wheelsOnGround == 2);

    LeanOf(bike).enabled = false;
    test.Run(3.0);
    INFO("off, it falls onto its left side (lean " << StateOf(bike).leanAngle / degree << "°)");
    REQUIRE(StateOf(bike).leanAngle < -60.0f * degree);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A light bike landing from a jump stays stable at 60 Hz", "[physics][vehicles][motorcycles]")
{
    // The sample's box gives a roll inertia of 10.4 kg m²: with Jolt's own lean damping it diverges, spinning up
    // tens of rad/s.
    PhysicsApp test;
    AddFloor(test.Entities());
    Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 4.0f, 0.0f), Math::AngleAxis(5.0f * degree, axisZ));
    bike.chassis.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f, 0.0f, -10.0f);
    float most = 0.0f;
    for (int i = 0; i < 300; ++i)
    {
        test.Run(tick);
        most = std::max(most, AngularSpeedOf(bike.chassis));
    }
    INFO("its angular speed never passes " << most << " rad/s");
    REQUIRE(most < 10.0f);
    INFO("it ends upright on both wheels (lean " << StateOf(bike).leanAngle / degree << "°)");
    REQUIRE(std::abs(StateOf(bike).leanAngle) < 2.0f * degree);
    REQUIRE(StateOf(bike).wheelsOnGround == 2);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Changing a motorcycle's maxAngle, or a vehicle's kind, rebuilds it keeping the engine speed, gear and spin",
          "[physics][vehicles][motorcycles][rebuilds]")
{
    PhysicsApp test;
    SECTION("maxAngle")
    {
        const Car bike = DrivenBike(test);
        const Before before = Read(bike);
        LeanOf(bike).maxAngle = 30.0f * degree;
        test.Run(tick);
        RequireCarried(bike, before);
        INFO("and it is still a motorcycle, upright");
        REQUIRE(VehicleOf(bike).kind.Is<MotorcycleVehicle>());
        REQUIRE(std::abs(StateOf(bike).leanAngle) < 1.0f * degree);
    }
    SECTION("motorcycle to wheeled, its drivetrain kept")
    {
        const Car bike = DrivenBike(test);
        const Before before = Read(bike);
        const MotorcycleVehicle motorcycle = VehicleOf(bike).kind.As<MotorcycleVehicle>();
        VehicleOf(bike).kind = WheeledVehicle{.engine = motorcycle.engine, .transmission = motorcycle.transmission};
        test.Run(tick);
        RequireCarried(bike, before);
        INFO("a wheeled vehicle reads no lean");
        REQUIRE(Exactly(StateOf(bike).leanAngle, 0.0f));
    }
    SECTION("wheeled to motorcycle")
    {
        AddFloor(test.Entities());
        const Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        VehicleOf(car).input.forward = 0.2f;
        test.Run(2.5);
        REQUIRE(StateOf(car).gear >= 1);
        const Before before = Read(car);
        VehicleOf(car).kind = MotorcycleVehicle{};
        test.Run(tick);
        RequireCarried(car, before);
        REQUIRE(VehicleOf(car).kind.Is<MotorcycleVehicle>());
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A motorcycle draws each of its wheels' debug lines, as a car does",
          "[physics][debug][vehicles][motorcycles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    const Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    REQUIRE(StateOf(bike).wheelsOnGround == 2);
    PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
    world.SetDebugDraw(DebugDraw::Vehicles);
    test.Run(tick);
    const auto count = [&world](std::uint32_t colour) {
        return std::ranges::count_if(world.DebugLines(),
                                     [colour](const DebugLine& line) { return line.colour == colour; });
    };
    INFO("two wheels' suspensions (3 lines each), circles (33) and contacts (4)");
    REQUIRE(count(DebugColour::Suspension) == 2 * 3);
    REQUIRE(count(DebugColour::Wheel) == 2 * 33);
    REQUIRE(world.DebugLines().size() == 2 * (3 + 33 + 4));
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Between ticks a leaning motorcycle is shown blended, its wheels on its blended chassis",
          "[physics][vehicles][motorcycles][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddFloor(test.Entities());
    const Car bike = AddMotorcycle(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    auto steer = std::make_shared<float>(0.0f);
    Drive(test.app, bike.chassis, [steer](int, Vehicle& vehicle) { Hold(vehicle, 40.0f * kmh, *steer); });
    test.Run(8.0);
    *steer = 1.0f;

    // The lean the last two steps left, recorded whenever a frame ticks.
    std::vector<float> leans = {StateOf(bike).leanAngle, StateOf(bike).leanAngle};
    float fraction = FractionOf(test.app);
    int blended = 0;
    for (int frame = 0; frame < 300; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const float now = FractionOf(test.app);
        if (now < fraction)
        {
            leans.push_back(StateOf(bike).leanAngle);
        }
        fraction = now;
        const Transform chassis = GetWorldTransform(bike.chassis);
        const float from = leans[leans.size() - 2];
        const float to = leans.back();
        const float shown = LeanOf(chassis);
        INFO("frame " << frame << ", " << fraction << " of a tick on: shown leaning " << shown / degree << "°, between "
                      << from / degree << "° and " << to / degree << "°");
        REQUIRE(shown >= std::min(from, to) - 1e-4f);
        REQUIRE(shown <= std::max(from, to) + 1e-4f);
        blended += fraction > 0.05f && std::abs(to - from) > 1e-4f ? 1 : 0;
        for (const Entity& wheel : bike.wheels)
        {
            const Math::Vec3 local = Math::Conjugate(chassis.rot) * (GetWorldTransform(wheel).pos - chassis.pos);
            INFO("each wheel stays in the chassis's middle plane (x " << local.x << ")");
            REQUIRE(local.x == Approx(0.0f).margin(1e-4f));
        }
    }
    INFO("most frames end part of the way through a tick, the lean changing");
    REQUIRE(blended > 100);
    REQUIRE(test.warnings.empty());
}
