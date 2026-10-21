// Trailers and towing through the frame seam (vehicles spec, kinds: TrailerVehicle; recipes: towing): a trailer
// braking and steering but ignoring `forward`, the gear, the clutch and differentials, its engine and gear state 0; a
// trailer on a ball joint towed by a car and waking with it, as a wheeled vehicle with no input is; and a kind change
// to and from trailer carrying over the wheels' spin.

#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // How far one tick may move a carried wheel spin: a fresh one would be off by far more.
    constexpr float carried = 0.03f;
    // The default engine's idle.
    constexpr float idleRpm = 1000.0f;
    // The speed a trailer is given to roll at: m/s.
    constexpr float rolling = 5.0f;

    Vehicle& VehicleOf(const Car& car)
    {
        return car.chassis.GetComponent<Vehicle>();
    }

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

    std::vector<float> SpinsOf(const std::vector<Entity>& wheels)
    {
        std::vector<float> spins;
        for (const Entity& wheel : wheels)
        {
            spins.push_back(StateOf(wheel).angularVelocity);
        }
        return spins;
    }

    void RequireSpinsCarried(const Car& car, const std::vector<float>& before)
    {
        for (std::size_t i = 0; i < car.wheels.size(); ++i)
        {
            const float spin = StateOf(car.wheels[i]).angularVelocity;
            INFO("wheel " << i << "'s spin carries over (" << before[i] << " to " << spin << " rad/s)");
            REQUIRE(spin == Approx(before[i]).epsilon(carried));
        }
    }

    void RequireNoDrivetrain(const Car& trailer)
    {
        const VehicleState& state = StateOf(trailer);
        INFO("a trailer's engine and gear fields stay 0");
        REQUIRE(Exactly(state.engineRpm, 0.0f));
        REQUIRE(state.gear == 0);
        REQUIRE(Exactly(state.clutch, 0.0f));
        REQUIRE_FALSE(state.shifting);
    }

    // The steered dolly settled on the floor, then rolling forward at `rolling`.
    Car RollingDolly(PhysicsApp& test)
    {
        AddFloor(test.Entities());
        Car trailer = AddTrailer(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), true);
        test.Run(1.0);
        REQUIRE(StateOf(trailer).wheelsOnGround == 4);
        trailer.chassis.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f, 0.0f, -rolling);
        test.Run(0.5);
        // Its still wheels take some of it, spun up by the tyres.
        INFO("rolling at " << StateOf(trailer).forwardSpeed << " m/s");
        REQUIRE(StateOf(trailer).forwardSpeed > 0.8f * rolling);
        return trailer;
    }
} // namespace

TEST_CASE("A trailer ignores forward, the gear, the clutch and a differential naming its wheels, its engine and gear "
          "state 0",
          "[physics][vehicles][trailers]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car trailer = AddTrailer(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), true);
    Entity differential = test.Entities().CreateEntity();
    differential.AddComponent<VehicleDifferential>(
        VehicleDifferential{.leftWheel = trailer.wheels[2], .rightWheel = trailer.wheels[3]});
    VehicleInput& input = VehicleOf(trailer).input;
    input.forward = 1.0f;
    input.gear = 2;
    input.clutch = 0.5f;
    test.Run(3.0);

    INFO("it stays where it was put (" << StateOf(trailer).forwardSpeed << " m/s)");
    REQUIRE(std::abs(StateOf(trailer).forwardSpeed) < 0.01f);
    REQUIRE(std::abs(PositionOf(trailer.chassis).z) < 0.01f);
    for (const Entity& wheel : trailer.wheels)
    {
        INFO("no wheel is driven (" << StateOf(wheel).angularVelocity << " rad/s)");
        REQUIRE(std::abs(StateOf(wheel).angularVelocity) < 0.05f);
    }
    REQUIRE(StateOf(trailer).wheelsOnGround == 4);
    RequireNoDrivetrain(trailer);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A rolling trailer slows under brake, coasts without, and steers with its dolly",
          "[physics][vehicles][trailers]")
{
    PhysicsApp test;
    Car trailer = RollingDolly(test);
    const float before = StateOf(trailer).forwardSpeed;
    SECTION("coasting, it keeps its speed")
    {
        test.Run(1.0);
        INFO(before << " to " << StateOf(trailer).forwardSpeed << " m/s");
        REQUIRE(StateOf(trailer).forwardSpeed > 0.85f * before);
    }
    SECTION("braked, it stops")
    {
        VehicleOf(trailer).input.brake = 1.0f;
        test.Run(2.0);
        INFO(before << " to " << StateOf(trailer).forwardSpeed << " m/s");
        REQUIRE(std::abs(StateOf(trailer).forwardSpeed) < 0.1f);
    }
    SECTION("steered right, its front wheels turn and it turns right")
    {
        VehicleOf(trailer).input.steer = 1.0f;
        test.Run(0.5);
        for (std::size_t i = 0; i < 2; ++i)
        {
            INFO("front wheel " << i << " steers " << StateOf(trailer.wheels[i]).steerAngle << " rad");
            REQUIRE(StateOf(trailer.wheels[i]).steerAngle == Approx(dollySteer).margin(0.01f));
        }
        INFO("the rear wheels do not");
        REQUIRE(std::abs(StateOf(trailer.wheels[2]).steerAngle) < 1e-4f);
        const float yawRate = trailer.chassis.GetComponent<RigidBody>().angularVelocity.y;
        INFO("turning right is clockwise from above (" << yawRate << " rad/s about +Y)");
        REQUIRE(yawRate < -0.5f);
    }
    RequireNoDrivetrain(trailer);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A trailer on a ball joint is towed by a car and wakes with it, as a wheeled vehicle with no input is",
          "[physics][vehicles][trailers]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    const Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    const Car trailer = AddTrailer(test.Entities(), Math::Vec3(0.0f, 1.0f, 2.3f + 1.9f), false);
    Hitch(test.Entities(), car, trailer);
    const bool trailerKind = GENERATE(true, false);
    if (!trailerKind)
    {
        VehicleOf(trailer).kind = WheeledVehicle{};
    }
    INFO((trailerKind ? "a TrailerVehicle" : "a WheeledVehicle with no input"));

    INFO("left alone, the two fall asleep together");
    REQUIRE(RunUntilAsleep(test, car.chassis));
    REQUIRE(World(test).IsSleeping(trailer.chassis));
    REQUIRE(StateOf(trailer).wheelsOnGround == 2);
    const float hitchGap = PositionOf(trailer.chassis).z - PositionOf(car.chassis).z;

    VehicleOf(car).input.forward = 0.2f;
    test.app.RunFrame(tick);
    INFO("the car's input wakes the trailer through the joint");
    REQUIRE_FALSE(World(test).IsSleeping(car.chassis));
    REQUIRE_FALSE(World(test).IsSleeping(trailer.chassis));

    test.Run(4.0);
    const float carSpeed = StateOf(car).forwardSpeed;
    const float trailerSpeed = StateOf(trailer).forwardSpeed;
    INFO("the car drives at " << carSpeed << " m/s and the trailer follows at " << trailerSpeed << " m/s");
    REQUIRE(carSpeed > 3.0f);
    REQUIRE(trailerSpeed == Approx(carSpeed).epsilon(0.05f));
    const float gap = PositionOf(trailer.chassis).z - PositionOf(car.chassis).z;
    INFO("the hitch holds the trailer " << gap << " m behind (was " << hitchGap << ")");
    REQUIRE(gap == Approx(hitchGap).margin(0.05f));
    REQUIRE(std::abs(PositionOf(trailer.chassis).x) < 0.05f);
    REQUIRE(StateOf(trailer).wheelsOnGround == 2);
    INFO("a wheeled vehicle with no input tows in neutral");
    REQUIRE(StateOf(trailer).gear == 0);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Changing kind wheeled to trailer and back reaches physics and keeps the wheels' spin",
          "[physics][vehicles][trailers][rebuilds]")
{
    PhysicsApp test;
    SECTION("wheeled to trailer: the engine stops driving")
    {
        AddFloor(test.Entities());
        const Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        VehicleOf(car).input.forward = 0.2f;
        test.Run(2.5);
        REQUIRE(StateOf(car).gear >= 1);
        const std::vector<float> spins = SpinsOf(car.wheels);
        VehicleOf(car).kind = TrailerVehicle{};
        test.Run(tick);
        RequireSpinsCarried(car, spins);
        RequireNoDrivetrain(car);

        const float before = StateOf(car).forwardSpeed;
        test.Run(1.0);
        INFO("its forward held, it no longer speeds up (" << before << " to " << StateOf(car).forwardSpeed << " m/s)");
        REQUIRE(StateOf(car).forwardSpeed <= before + 0.01f);
        RequireNoDrivetrain(car);
    }
    SECTION("trailer to wheeled: it starts at idle in neutral, then drives")
    {
        const Car trailer = RollingDolly(test);
        const std::vector<float> spins = SpinsOf(trailer.wheels);
        VehicleOf(trailer).kind = WheeledVehicle{};
        test.Run(tick);
        RequireSpinsCarried(trailer, spins);
        INFO("it starts at idle (" << StateOf(trailer).engineRpm << " RPM) in neutral");
        REQUIRE(StateOf(trailer).engineRpm == Approx(idleRpm).epsilon(0.05f));
        REQUIRE(StateOf(trailer).gear == 0);

        const float before = StateOf(trailer).forwardSpeed;
        VehicleOf(trailer).input.forward = 0.5f;
        test.Run(2.0);
        INFO("driven, it speeds up (" << before << " to " << StateOf(trailer).forwardSpeed << " m/s)");
        REQUIRE(StateOf(trailer).gear >= 1);
        REQUIRE(StateOf(trailer).forwardSpeed > before + 1.0f);
    }
    REQUIRE(test.warnings.empty());
}
