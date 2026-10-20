// Teleports, rebuilds and wheels lost mid-drive (ADR-0015) through the frame seam: a chassis teleport keeping engine
// speed and wheel spin and snapping wheel poses, a wheel-set change or a chassis body rebuilt in one drain carrying
// over what still maps, a chassis scale change patching wheel positions in place, and differentials and anti-roll
// bars naming a wheel that has died.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // The default engine's idle.
    constexpr float idleRpm = 1000.0f;
    // How far one tick may move a carried engine speed or wheel spin: a fresh one would be off by far more.
    constexpr float carried = 0.03f;
    // A wheel the engine has spun up in the air, and one it has not.
    constexpr float drivenSpin = 1.0f;
    constexpr float stillSpin = 1e-3f;

    std::vector<float> SpinsOf(const std::vector<Entity>& wheels)
    {
        std::vector<float> spins;
        for (const Entity& wheel : wheels)
        {
            spins.push_back(StateOf(wheel).angularVelocity);
        }
        return spins;
    }

    std::size_t CountWarnings(const PhysicsApp& test, const std::string& about)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            test.warnings, [&about](const std::string& warning) { return warning.find(about) != std::string::npos; }));
    }

    // What a rebuild or teleport should carry over, read after the tick before it.
    struct Before
    {
        float engineRpm;
        int gear;
        float forwardSpeed;
        std::vector<float> spins;
    };

    Before Read(const Car& car)
    {
        return Before{.engineRpm = StateOf(car).engineRpm,
                      .gear = StateOf(car).gear,
                      .forwardSpeed = StateOf(car).forwardSpeed,
                      .spins = SpinsOf(car.wheels)};
    }

    /** The car driven gently on the floor for two and a half seconds, its engine above idle in first gear, its wheels
     * rolling without slipping. A differential drives each pair of wheels, so a wheel added later is not driven. */
    Car DrivenCar(PhysicsApp& test)
    {
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        for (std::size_t pair = 0; pair < 2; ++pair)
        {
            Entity differential = test.Entities().CreateEntity();
            differential.AddComponent<VehicleDifferential>(
                VehicleDifferential{.leftWheel = car.wheels[2 * pair], .rightWheel = car.wheels[(2 * pair) + 1]});
        }
        car.chassis.GetComponent<Vehicle>().input.forward = 0.25f;
        test.Run(2.5);
        INFO("driven: " << StateOf(car).engineRpm << " RPM in gear " << StateOf(car).gear);
        REQUIRE(StateOf(car).gear >= 1);
        REQUIRE(StateOf(car).engineRpm > idleRpm + 200.0f);
        for (const Entity& wheel : car.wheels)
        {
            REQUIRE(StateOf(wheel).angularVelocity > 5.0f);
        }
        return car;
    }

    /** The engine speed, gear, speed and each wheel's spin carried over one tick from `before`. */
    void RequireCarried(const Car& car, const Before& before)
    {
        const VehicleState& state = StateOf(car);
        INFO("the engine speed carries over (" << before.engineRpm << " to " << state.engineRpm << " RPM)");
        REQUIRE(state.engineRpm == Approx(before.engineRpm).epsilon(carried));
        INFO("so does the gear");
        REQUIRE(state.gear == before.gear);
        INFO("and the chassis's speed (" << before.forwardSpeed << " to " << state.forwardSpeed << " m/s)");
        REQUIRE(state.forwardSpeed == Approx(before.forwardSpeed).epsilon(carried));
        for (std::size_t i = 0; i < car.wheels.size(); ++i)
        {
            const float spin = StateOf(car.wheels[i]).angularVelocity;
            INFO("wheel " << i << "'s spin carries over (" << before.spins[i] << " to " << spin << " rad/s)");
            REQUIRE(spin == Approx(before.spins[i]).epsilon(carried));
        }
    }

    // Where the wheel's centre should be: its position raised by its compression, on its chassis's pose.
    Math::Vec3 PosedAt(const Entity& chassis, const Entity& wheel)
    {
        const Transform at = GetWorldTransform(chassis);
        const VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
        const Math::Vec3 local = settings.position * at.scale + Math::Vec3(0.0f, settings.state.compression, 0.0f);
        return at.pos + at.rot * local;
    }

    // The car on a stand under its chassis, every wheel in the air.
    Car AddCarOnStand(EntityList& entities)
    {
        Entity stand = Place(entities, Math::Vec3(0.0f, 0.5f, 0.0f));
        stand.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f, 0.5f, 1.0f)}});
        return AddCar(entities, Math::Vec3(0.0f, 1.3f, 0.0f));
    }

    Entity AddWheel(EntityList& entities, const Entity& chassis, const Math::Vec3& position)
    {
        Entity wheel = Place(entities, Math::Vec3(0.0f));
        wheel.AddComponent<VehicleWheel>(VehicleWheel{.vehicle = chassis, .position = position});
        return wheel;
    }
} // namespace

TEST_CASE("A chassis teleport keeps the engine speed and wheel spin, and snaps the wheel poses",
          "[physics][vehicles][rebuilds]")
{
    PhysicsApp test;
    Car car = DrivenCar(test);
    const Before before = Read(car);

    Transform& at = car.chassis.GetComponent<Transform>();
    at.pos += Math::Vec3(30.0f, 0.0f, 0.0f);
    test.Run(tick);
    RequireCarried(car, before);
    for (std::size_t i = 0; i < car.wheels.size(); ++i)
    {
        const Math::Vec3 expected = PosedAt(car.chassis, car.wheels[i]);
        const Math::Vec3 actual = GetWorldTransform(car.wheels[i]).pos;
        INFO("wheel " << i << " is posed on the teleported chassis at once (off by "
                      << Math::Magnitude(actual - expected) << " m)");
        REQUIRE(Math::Magnitude(actual - expected) < 1e-4f);
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE(
    "Adding a wheel mid-drive keeps the engine speed, gear and every surviving wheel's spin; the new wheel starts "
    "still",
    "[physics][vehicles][rebuilds]")
{
    PhysicsApp test;
    Car car = DrivenCar(test);
    const Before before = Read(car);

    // On the roof, where it touches nothing, and in no differential: nothing spins it.
    Entity added = AddWheel(test.Entities(), car.chassis, Math::Vec3(0.0f, 0.6f, 0.0f));
    test.Run(tick);
    RequireCarried(car, before);
    INFO("the new wheel starts still (" << StateOf(added).angularVelocity << " rad/s)");
    REQUIRE(std::abs(StateOf(added).angularVelocity) < stillSpin);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A chassis whose body is rebuilt in one drain carries over its engine speed, gear and wheel spin",
          "[physics][vehicles][rebuilds]")
{
    PhysicsApp test;
    Car car = DrivenCar(test);
    const Before before = Read(car);

    SECTION("its collider swapped")
    {
        const Collider collider = car.chassis.GetComponent<Collider>();
        car.chassis.RemoveComponent<Collider>();
        car.chassis.AddComponent<Collider>(collider);
    }
    SECTION("its RigidBody removed and added")
    {
        const RigidBody body = car.chassis.GetComponent<RigidBody>();
        car.chassis.RemoveComponent<RigidBody>();
        car.chassis.AddComponent<RigidBody>(body);
    }
    test.Run(tick);
    RequireCarried(car, before);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A chassis scale change moves the wheel positions in place, leaving the engine and spin, and wakes the "
          "chassis",
          "[physics][vehicles][rebuilds]")
{
    SECTION("while driving, the engine speed and spin carry on")
    {
        PhysicsApp test;
        Car car = DrivenCar(test);
        const Before before = Read(car);

        car.chassis.GetComponent<Transform>().scale = Math::Vec3(1.1f);
        test.Run(tick);
        RequireCarried(car, before);
        for (std::size_t i = 0; i < car.wheels.size(); ++i)
        {
            const Math::Vec3 expected = PosedAt(car.chassis, car.wheels[i]);
            const Math::Vec3 actual = GetWorldTransform(car.wheels[i]).pos;
            INFO("wheel " << i << " is posed at its scaled position (off by " << Math::Magnitude(actual - expected)
                          << " m)");
            REQUIRE(Math::Magnitude(actual - expected) < 1e-4f);
        }
        REQUIRE(test.warnings.empty());
    }

    SECTION("parked, it wakes")
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        PhysicsWorld* world = nullptr;
        for (int frame = 0; frame < 20 * 60; ++frame)
        {
            test.app.RunFrame(tick);
            world = PhysicsWorld::Of(test.app.GetActiveScene());
            if (world->IsSleeping(car.chassis))
            {
                break;
            }
        }
        REQUIRE(world->IsSleeping(car.chassis));

        car.chassis.GetComponent<Transform>().scale = Math::Vec3(1.1f);
        test.app.RunFrame(tick);
        INFO("scaling the sleeping chassis wakes it");
        REQUIRE_FALSE(PhysicsWorld::Of(test.app.GetActiveScene())->IsSleeping(car.chassis));
        const Math::Vec3 local = Math::Conjugate(GetWorldTransform(car.chassis).rot) *
                                 (GetWorldTransform(car.wheels[0]).pos - GetWorldTransform(car.chassis).pos);
        INFO("and its wheels sit wider apart (front left at x = " << local.x << ")");
        REQUIRE(local.x == Approx(-wheelX * 1.1f).margin(1e-4f));
        REQUIRE(test.warnings.empty());
    }
}

TEST_CASE("A rear differential with one wheel destroyed drives the other, with one warning, until named again",
          "[physics][vehicles][rebuilds]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    Entity differential = test.Entities().CreateEntity();
    differential.AddComponent<VehicleDifferential>(
        VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3]});
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.5f; });
    test.Run(0.5);

    SECTION("its entity destroyed")
    {
        test.Entities().DestroyEntity(car.wheels[2]);
    }
    SECTION("its VehicleWheel removed")
    {
        car.wheels[2].RemoveComponent<VehicleWheel>();
    }
    const std::vector<Entity> left = {car.wheels[0], car.wheels[1], car.wheels[3]};
    const std::vector<float> before = SpinsOf(left);
    test.Run(1.0);
    std::vector<float> spins = SpinsOf(left);

    INFO("one warning, naming the dead wheel, and the differential is not inactive");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(CountWarnings(test, "VehicleDifferential on entity " + std::to_string(differential.GetId()) +
                                    " names entity " + std::to_string(car.wheels[2].GetId()) +
                                    ", whose wheel has died") == 1);
    for (std::size_t i = 0; i < 2; ++i)
    {
        INFO("front wheel " << i << " stays still (" << spins[i] << " rad/s)");
        REQUIRE(std::abs(spins[i]) < stillSpin);
    }
    INFO("the rear right wheel, the one left, is still driven: it speeds up (" << before[2] << " to " << spins[2]
                                                                               << " rad/s)");
    REQUIRE(spins[2] > before[2]);

    // Remade, as the DemoApp's 8 does: a new wheel in its place, named in the dead one's place.
    Entity remade = AddWheel(test.Entities(), car.chassis, Math::Vec3(-wheelX, wheelY, wheelZ));
    differential.GetComponent<VehicleDifferential>().leftWheel = remade;
    test.Run(0.5);
    INFO("named again, the remade wheel is driven, with no more warnings");
    REQUIRE(StateOf(remade).angularVelocity > drivenSpin);
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("An anti-roll bar with one wheel destroyed is inert, with one warning", "[physics][vehicles][rebuilds]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    // A middle pair as well, so the car stands on its other corners with a rear wheel gone.
    AddWheel(test.Entities(), car.chassis, Math::Vec3(-wheelX, wheelY, 0.0f));
    AddWheel(test.Entities(), car.chassis, Math::Vec3(wheelX, wheelY, 0.0f));
    Entity bar = test.Entities().CreateEntity();
    bar.AddComponent<VehicleAntiRollBar>(
        VehicleAntiRollBar{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3], .stiffness = 10000.0f});
    test.Run(1.0);
    REQUIRE(StateOf(car).wheelsOnGround == 6);

    test.Entities().DestroyEntity(car.wheels[2]);
    test.Run(1.0);
    INFO("one warning, naming the dead wheel, and the bar is not inactive");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(CountWarnings(test, "VehicleAntiRollBar on entity " + std::to_string(bar.GetId()) + " names entity " +
                                    std::to_string(car.wheels[2].GetId()) + ", whose wheel has died") == 1);
    INFO("the car stands on its five wheels left, the bar pushing on none of them");
    REQUIRE(StateOf(car).wheelsOnGround == 5);
}

TEST_CASE("A vehicle's only differential, with no wheels left, leaves it driving every wheel",
          "[physics][vehicles][rebuilds]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    Entity differential = test.Entities().CreateEntity();
    differential.AddComponent<VehicleDifferential>(VehicleDifferential{.rightWheel = car.wheels[3]});
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.5f; });
    test.Run(0.5);
    REQUIRE(std::abs(StateOf(car.wheels[0]).angularVelocity) < stillSpin);

    test.Entities().DestroyEntity(car.wheels[3]);
    test.Run(1.0);
    INFO("one warning about the dead name");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(CountWarnings(test, "VehicleDifferential on entity " + std::to_string(differential.GetId())) == 1);
    for (std::size_t i = 0; i < 3; ++i)
    {
        const float spin = StateOf(car.wheels[i]).angularVelocity;
        INFO("wheel " << i << ", in no differential before, is driven now (" << spin << " rad/s)");
        REQUIRE(spin > drivenSpin);
    }
}
