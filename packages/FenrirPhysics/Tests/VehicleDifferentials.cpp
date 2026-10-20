// Differentials and anti-roll bars (ADR-0015) through the frame seam: which wheels the engine drives and how it shares
// its torque out, anti-roll bars against roll, the components naming wrong wheels, out-of-range values, and edits
// waking the chassis.

#include <algorithm>
#include <cmath>
#include <limits>
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
    // The car on a stand under its chassis, every wheel in the air: a driven wheel spins up and an undriven one stays
    // still, whatever the ground would do.
    Car AddCarOnStand(EntityList& entities)
    {
        Entity stand = Place(entities, Math::Vec3(0.0f, 0.5f, 0.0f));
        stand.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f, 0.5f, 1.0f)}});
        return AddCar(entities, Math::Vec3(0.0f, 1.3f, 0.0f));
    }

    // Each wheel's spin, rad/s.
    std::vector<float> SpinsOf(const Car& car)
    {
        std::vector<float> spins;
        for (const Entity& wheel : car.wheels)
        {
            spins.push_back(StateOf(wheel).angularVelocity);
        }
        return spins;
    }

    // A wheel the engine has spun up in the air, and one it has not.
    constexpr float drivenSpin = 1.0f;
    constexpr float stillSpin = 1e-3f;

    // Throttle on the stand for `seconds`; each wheel's spin then.
    std::vector<float> SpinOnStand(PhysicsApp& test, const Car& car, double seconds = 1.0)
    {
        Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.5f; });
        test.Run(seconds);
        return SpinsOf(car);
    }

    // The car's open between differentials: each differential's torque share holds whatever its wheels' speeds.
    void OpenBetweenDifferentials(const Car& car)
    {
        car.chassis.GetComponent<Vehicle>().kind.As<WheeledVehicle>().limitedSlipRatio =
            std::numeric_limits<float>::max();
    }

    Entity AddDifferential(EntityList& entities, const VehicleDifferential& differential)
    {
        Entity entity = entities.CreateEntity();
        entity.AddComponent<VehicleDifferential>(differential);
        return entity;
    }

    Entity AddAntiRollBar(EntityList& entities, const VehicleAntiRollBar& bar)
    {
        Entity entity = entities.CreateEntity();
        entity.AddComponent<VehicleAntiRollBar>(bar);
        return entity;
    }

    // Runs until the chassis sleeps, for at most 20 s.
    PhysicsWorld& SleepOn(PhysicsApp& test, const Car& car)
    {
        for (int frame = 0; frame < 20 * 60; ++frame)
        {
            test.app.RunFrame(tick);
            if (PhysicsWorld::Of(test.app.GetActiveScene())->IsSleeping(car.chassis))
            {
                break;
            }
        }
        PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
        REQUIRE(world.IsSleeping(car.chassis));
        return world;
    }

    std::size_t CountWarnings(const PhysicsApp& test, const std::string& about)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            test.warnings, [&about](const std::string& warning) { return warning.find(about) != std::string::npos; }));
    }
} // namespace

TEST_CASE("A differential and an anti-roll bar made with no arguments have the spec's defaults", "[physics][vehicles]")
{
    const VehicleDifferential differential;
    INFO("a differential names no wheels, 3.42 to 1, split evenly, limited-slip at 1.4, with a share of 1");
    REQUIRE(differential.leftWheel == Entity());
    REQUIRE(differential.rightWheel == Entity());
    REQUIRE(Exactly(differential.ratio, 3.42f));
    REQUIRE(Exactly(differential.leftRightSplit, 0.5f));
    REQUIRE(Exactly(differential.limitedSlipRatio, 1.4f));
    REQUIRE(Exactly(differential.torqueShare, 1.0f));

    const VehicleAntiRollBar bar;
    INFO("an anti-roll bar names no wheels, at 1000 N/m");
    REQUIRE(bar.leftWheel == Entity());
    REQUIRE(bar.rightWheel == Entity());
    REQUIRE(Exactly(bar.stiffness, 1000.0f));
}

TEST_CASE("With no differential the engine drives every wheel equally", "[physics][vehicles]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    const std::vector<float> spins = SpinOnStand(test, car);

    for (std::size_t i = 0; i < spins.size(); ++i)
    {
        INFO("wheel " << i << " spins forward in the air (" << spins[i] << " rad/s), as fast as wheel 0");
        REQUIRE(spins[i] > drivenSpin);
        REQUIRE(spins[i] == Approx(spins[0]).epsilon(0.01));
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A rear differential drives only its two wheels, and the front wheels roll with the ground",
          "[physics][vehicles]")
{
    SECTION("on a stand, only the rear wheels spin up")
    {
        PhysicsApp test;
        Car car = AddCarOnStand(test.Entities());
        AddDifferential(test.Entities(), VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3]});
        const std::vector<float> spins = SpinOnStand(test, car);

        for (std::size_t i = 0; i < 2; ++i)
        {
            INFO("front wheel " << i << ", in no differential, stays still (" << spins[i] << " rad/s)");
            REQUIRE(std::abs(spins[i]) < stillSpin);
        }
        for (std::size_t i = 2; i < 4; ++i)
        {
            INFO("rear wheel " << i << " spins up (" << spins[i] << " rad/s)");
            REQUIRE(spins[i] > drivenSpin);
        }
        REQUIRE(test.warnings.empty());
    }

    SECTION("on the ground, the car drives off and its front wheels turn at the ground's speed")
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        AddDifferential(test.Entities(), VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3]});
        Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.2f; });
        test.Run(3.0);

        const float speed = StateOf(car).forwardSpeed;
        INFO("it drives forward (" << speed << " m/s)");
        REQUIRE(speed > 1.0f);
        for (std::size_t i = 0; i < 2; ++i)
        {
            const float rolled = StateOf(car.wheels[i]).angularVelocity * VehicleWheel{}.radius;
            INFO("front wheel " << i << " rolls at " << rolled << " m/s, the ground's speed");
            REQUIRE(rolled == Approx(speed).epsilon(0.03));
        }
    }
}

TEST_CASE("A differential naming one wheel drives just that wheel", "[physics][vehicles]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    AddDifferential(test.Entities(), VehicleDifferential{.rightWheel = car.wheels[3]});
    const std::vector<float> spins = SpinOnStand(test, car);

    for (std::size_t i = 0; i < 3; ++i)
    {
        INFO("wheel " << i << " stays still (" << spins[i] << " rad/s)");
        REQUIRE(std::abs(spins[i]) < stillSpin);
    }
    INFO("the rear right wheel, the one named, spins up (" << spins[3] << " rad/s)");
    REQUIRE(spins[3] > drivenSpin);
}

TEST_CASE("Two differentials with torque shares 3 and 1 take 0.75 and 0.25 of the torque", "[physics][vehicles]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    OpenBetweenDifferentials(car);
    AddDifferential(test.Entities(),
                    VehicleDifferential{.leftWheel = car.wheels[0], .rightWheel = car.wheels[1], .torqueShare = 3.0f});
    AddDifferential(test.Entities(),
                    VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3], .torqueShare = 1.0f});
    const std::vector<float> spins = SpinOnStand(test, car, 0.5);

    // From rest with nothing holding them back, equal wheels spin up in proportion to their torque.
    const float front = 0.5f * (spins[0] + spins[1]);
    const float rear = 0.5f * (spins[2] + spins[3]);
    INFO("the front wheels spin " << front << " rad/s and the rear " << rear << ": three times as fast");
    REQUIRE(rear > drivenSpin);
    REQUIRE(front / rear == Approx(3.0f).epsilon(0.02));
    REQUIRE(test.warnings.empty());
}

namespace
{
    // The chassis's mean roll, radians, over the last second of a steady right turn: + rolls its right side up.
    float RollInTurn(bool bars)
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        if (bars)
        {
            AddAntiRollBar(
                test.Entities(),
                VehicleAntiRollBar{.leftWheel = car.wheels[0], .rightWheel = car.wheels[1], .stiffness = 30000.0f});
            AddAntiRollBar(
                test.Entities(),
                VehicleAntiRollBar{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3], .stiffness = 30000.0f});
        }
        Drive(test.app, car.chassis, [](int index, Vehicle& vehicle) {
            vehicle.input.forward = 0.3f;
            vehicle.input.steer = index > 120 ? 0.4f : 0.0f;
        });
        test.Run(4.0);
        float roll = 0.0f;
        const int samples = 60;
        for (int i = 0; i < samples; ++i)
        {
            test.app.RunFrame(tick);
            const Math::Vec3 right = GetWorldTransform(car.chassis).rot * Math::Vec3(1.0f, 0.0f, 0.0f);
            roll += std::asin(std::clamp(right.y, -1.0f, 1.0f));
        }
        REQUIRE(test.warnings.empty());
        return roll / static_cast<float>(samples);
    }
} // namespace

TEST_CASE("Anti-roll bars reduce the chassis's roll in a steady turn", "[physics][vehicles]")
{
    const float without = RollInTurn(false);
    const float with = RollInTurn(true);
    INFO("turning right, the car leans out, its right side up: " << without << " rad without bars, " << with
                                                                 << " with them");
    REQUIRE(without > 0.005f);
    REQUIRE(std::abs(with) < 0.5f * without);
}

TEST_CASE("A differential or anti-roll bar naming a non-wheel is inactive, with one warning, until fixed",
          "[physics][vehicles]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    Entity plain = test.Entities().CreateEntity();
    Entity differential =
        AddDifferential(test.Entities(), VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = plain});
    Entity bar =
        AddAntiRollBar(test.Entities(), VehicleAntiRollBar{.leftWheel = car.chassis, .rightWheel = car.wheels[3]});
    std::vector<float> spins = SpinOnStand(test, car);

    INFO("each warned about once over a second: " << test.warnings.size() << " warnings");
    REQUIRE(test.warnings.size() == 2);
    REQUIRE(CountWarnings(test, "VehicleDifferential on entity " + std::to_string(differential.GetId()) +
                                    " is inactive") == 1);
    REQUIRE(CountWarnings(test, "VehicleAntiRollBar on entity " + std::to_string(bar.GetId()) + " is inactive") == 1);
    INFO("the inactive differential replaces nothing: every wheel is still driven (front " << spins[0] << ")");
    REQUIRE(spins[0] > drivenSpin);

    differential.GetComponent<VehicleDifferential>().rightWheel = car.wheels[3];
    bar.GetComponent<VehicleAntiRollBar>().leftWheel = car.wheels[2];
    test.Run(0.1);
    const std::vector<float> before = SpinsOf(car);
    test.Run(0.5);
    spins = SpinsOf(car);
    INFO("named right, it drives the rear wheels only: the front wheels slow (" << before[0] << " to " << spins[0]
                                                                                << ") and the rear speed up");
    REQUIRE(spins[0] < before[0]);
    REQUIRE(spins[2] > before[2]);
    INFO("with no more warnings");
    REQUIRE(test.warnings.size() == 2);

    differential.GetComponent<VehicleDifferential>().leftWheel = plain;
    test.Run(0.2);
    INFO("named wrong again, it is warned about again, once");
    REQUIRE(test.warnings.size() == 3);
}

TEST_CASE("A differential or anti-roll bar naming wheels of two vehicles is inactive, with one warning, until fixed",
          "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Car other = AddCar(test.Entities(), Math::Vec3(5.0f, 1.0f, 0.0f));
    Entity differential = AddDifferential(
        test.Entities(), VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = other.wheels[3]});
    Entity bar =
        AddAntiRollBar(test.Entities(), VehicleAntiRollBar{.leftWheel = other.wheels[0], .rightWheel = car.wheels[1]});
    test.Run(1.0);

    INFO("each warned about once over a second");
    REQUIRE(test.warnings.size() == 2);
    REQUIRE(CountWarnings(test, "two different vehicles") == 2);

    differential.GetComponent<VehicleDifferential>().rightWheel = car.wheels[3];
    bar.GetComponent<VehicleAntiRollBar>().leftWheel = car.wheels[0];
    test.Run(1.0);
    INFO("named as one vehicle's, both are active, with no more warnings");
    REQUIRE(test.warnings.size() == 2);
}

TEST_CASE("Out-of-range differential and anti-roll-bar values are corrected, with one warning each",
          "[physics][vehicles]")
{
    PhysicsApp test;
    Car car = AddCarOnStand(test.Entities());
    Entity differential = AddDifferential(
        test.Entities(),
        VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3], .limitedSlipRatio = 0.5f});
    Entity bar =
        AddAntiRollBar(test.Entities(),
                       VehicleAntiRollBar{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3], .stiffness = -5.0f});
    const std::vector<float> spins = SpinOnStand(test, car, 2.0);

    INFO("a limited-slip ratio of 0.5 is raised to just above 1, and the rear wheels are driven");
    REQUIRE(CountWarnings(test, "VehicleDifferential on entity " + std::to_string(differential.GetId()) +
                                    ": a limited-slip ratio at or below 1") == 1);
    REQUIRE(spins[2] > drivenSpin);
    INFO("a negative stiffness is treated as 0");
    REQUIRE(CountWarnings(test, "VehicleAntiRollBar on entity " + std::to_string(bar.GetId()) +
                                    ": a negative stiffness") == 1);
    INFO("and nothing else, over two seconds");
    REQUIRE(test.warnings.size() == 2);
    INFO("game code's values are left as written");
    REQUIRE(Exactly(differential.GetComponent<VehicleDifferential>().limitedSlipRatio, 0.5f));
    REQUIRE(Exactly(bar.GetComponent<VehicleAntiRollBar>().stiffness, -5.0f));
}

TEST_CASE("Adding or editing a differential or anti-roll bar wakes a sleeping chassis", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));

    PhysicsWorld* world = &SleepOn(test, car);
    Entity differential =
        AddDifferential(test.Entities(), VehicleDifferential{.leftWheel = car.wheels[2], .rightWheel = car.wheels[3]});
    test.app.RunFrame(tick);
    INFO("adding a differential wakes it");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));

    world = &SleepOn(test, car);
    differential.GetComponent<VehicleDifferential>().ratio = 4.0f;
    test.app.RunFrame(tick);
    INFO("editing the differential's ratio wakes it");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));

    world = &SleepOn(test, car);
    differential.GetComponent<VehicleDifferential>().leftWheel = car.wheels[0];
    test.app.RunFrame(tick);
    INFO("relinking a wheel wakes it");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));

    world = &SleepOn(test, car);
    Entity bar =
        AddAntiRollBar(test.Entities(), VehicleAntiRollBar{.leftWheel = car.wheels[0], .rightWheel = car.wheels[1]});
    test.app.RunFrame(tick);
    INFO("adding an anti-roll bar wakes it");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));

    world = &SleepOn(test, car);
    bar.GetComponent<VehicleAntiRollBar>().stiffness = 5000.0f;
    test.app.RunFrame(tick);
    INFO("editing its stiffness wakes it");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));

    world = &SleepOn(test, car);
    test.Entities().DestroyEntity(differential);
    test.app.RunFrame(tick);
    INFO("removing the differential wakes it, back to driving every wheel");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));
    REQUIRE(test.warnings.empty());
}
