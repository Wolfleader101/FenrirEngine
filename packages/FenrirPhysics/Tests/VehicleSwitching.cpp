// Vehicles switched off: disabled by game code (`enabled = false`), or inactive because the chassis cannot be a
// vehicle's (no dynamic body with a solid collider, or beside a CharacterBody) or a wheel cannot be one of its wheels,
// each warned about once. Also a Joint naming a wheel entity.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // The default engine's idle.
    constexpr float idleRpm = 1000.0f;

    /** How many warnings say `component` (with its preposition: "Vehicle on") on `entity` went inactive. */
    std::ptrdiff_t WarningsAbout(const PhysicsApp& test, const std::string& component, const Entity& entity)
    {
        const std::string about = "The " + component + " entity " + std::to_string(entity.GetId()) + " is inactive";
        return std::ranges::count_if(test.warnings,
                                     [&about](const std::string& warning) { return warning.starts_with(about); });
    }

    void RequireReset(const VehicleState& state)
    {
        REQUIRE(Exactly(state.forwardSpeed, 0.0f));
        REQUIRE(Exactly(state.engineRpm, 0.0f));
        REQUIRE(state.gear == 0);
        REQUIRE(Exactly(state.clutch, 0.0f));
        REQUIRE_FALSE(state.shifting);
        REQUIRE(state.wheelsOnGround == 0);
    }

    void RequireReset(const VehicleWheelState& state)
    {
        REQUIRE_FALSE(state.onGround);
        REQUIRE_FALSE(state.groundBody.IsValid());
        REQUIRE_FALSE(state.groundCollider.IsValid());
        REQUIRE(Exactly(state.compression, 0.0f));
        REQUIRE(Exactly(state.suspensionForce, 0.0f));
        REQUIRE(Exactly(state.angularVelocity, 0.0f));
        REQUIRE(Exactly(state.steerAngle, 0.0f));
    }

    bool SamePose(const Transform& a, const Transform& b)
    {
        return std::memcmp(&a.pos, &b.pos, sizeof(a.pos)) == 0 && std::memcmp(&a.rot, &b.rot, sizeof(a.rot)) == 0;
    }

    /** The car driven gently for two seconds, so its engine is above idle in a forward gear. */
    Car DrivenCar(PhysicsApp& test)
    {
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        car.chassis.GetComponent<Vehicle>().input.forward = 0.2f;
        test.Run(2.0);
        REQUIRE(StateOf(car).gear >= 1);
        REQUIRE(StateOf(car).engineRpm > idleRpm + 100.0f);
        return car;
    }
} // namespace

TEST_CASE("A disabled vehicle is a plain body resting on its chassis, its states reset and its wheels left in place; "
          "enabled again it starts fresh",
          "[physics][vehicles][inactive]")
{
    PhysicsApp test;
    Car car = DrivenCar(test);
    std::vector<Transform> wheels;
    for (const Entity& wheel : car.wheels)
    {
        wheels.push_back(GetWorldTransform(wheel));
    }

    // The input is left as it was: a disabled vehicle ignores it.
    car.chassis.GetComponent<Vehicle>().enabled = false;
    test.Run(3.0);
    INFO("the chassis rolled to a stop and came to rest on its box, at y = " << PositionOf(car.chassis).y);
    REQUIRE(PositionOf(car.chassis).y == Approx(0.25f).margin(restSlop));
    INFO("its state and every wheel's are reset, and each wheel entity is where it was when the vehicle was disabled");
    RequireReset(StateOf(car));
    for (std::size_t i = 0; i < car.wheels.size(); ++i)
    {
        RequireReset(StateOf(car.wheels[i]));
        REQUIRE(SamePose(GetWorldTransform(car.wheels[i]), wheels[i]));
    }
    REQUIRE(test.warnings.empty());

    car.chassis.GetComponent<Vehicle>().input.forward = 0.0f;
    car.chassis.GetComponent<Vehicle>().enabled = true;
    test.Run(tick);
    INFO("enabled again, it is built fresh: at idle in neutral, carrying nothing over");
    REQUIRE(StateOf(car).gear == 0);
    REQUIRE(StateOf(car).engineRpm == Approx(idleRpm).margin(1.0f));
    test.Run(1.0);
    INFO("it rose back onto its wheels, and drives off");
    REQUIRE(StateOf(car).wheelsOnGround == 4);
    const Math::Vec3 parked = PositionOf(car.chassis);
    car.chassis.GetComponent<Vehicle>().input.forward = 0.2f;
    test.Run(2.0);
    REQUIRE(PositionOf(car.chassis).z < parked.z - 1.0f);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Each kind of inactive vehicle warns once on going inactive, resets its state and comes back once valid",
          "[physics][vehicles][inactive]")
{
    struct Case
    {
        const char* what;
        std::function<void(Entity&)> invalidate;
        std::function<void(Entity&)> restore;
    };
    const auto motion = [](MotionType type) {
        return [type](Entity& chassis) { chassis.GetComponent<RigidBody>().motionType = type; };
    };
    const std::vector<Case> cases = {
        {"made kinematic", motion(MotionType::Kinematic), motion(MotionType::Dynamic)},
        {"made static", motion(MotionType::Static), motion(MotionType::Dynamic)},
        {"its RigidBody removed", [](Entity& chassis) { chassis.RemoveComponent<RigidBody>(); },
         [](Entity& chassis) { chassis.AddComponent<RigidBody>(RigidBody{.mass = carMass}); }},
        {"its Collider removed", [](Entity& chassis) { chassis.RemoveComponent<Collider>(); },
         [](Entity& chassis) { chassis.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.9f, 0.25f, 2.0f)}}); }},
        {"its collider made a trigger", [](Entity& chassis) { chassis.AddComponent<Trigger>(); },
         [](Entity& chassis) { chassis.RemoveComponent<Trigger>(); }},
        {"beside a CharacterBody", [](Entity& chassis) { chassis.AddComponent<CharacterBody>(); },
         [](Entity& chassis) { chassis.RemoveComponent<CharacterBody>(); }},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        test.Run(1.0);
        REQUIRE(StateOf(car).wheelsOnGround == 4);
        REQUIRE(test.warnings.empty());

        c.invalidate(car.chassis);
        test.Run(0.25);
        INFO(c.what << ": the vehicle gave one warning on going inactive, and no more while it stayed inactive");
        REQUIRE(WarningsAbout(test, "Vehicle on", car.chassis) == 1);
        INFO("its state and its wheels' are reset");
        RequireReset(StateOf(car));
        RequireReset(StateOf(car.wheels[0]));

        c.restore(car.chassis);
        test.Run(1.0);
        INFO("valid again, it is a vehicle on its four wheels, with no further warning");
        REQUIRE(StateOf(car).wheelsOnGround == 4);
        REQUIRE(WarningsAbout(test, "Vehicle on", car.chassis) == 1);

        c.invalidate(car.chassis);
        test.Run(tick);
        INFO("going inactive again warns again");
        REQUIRE(WarningsAbout(test, "Vehicle on", car.chassis) == 2);
    }
}

TEST_CASE("A chassis made kinematic for a cutscene and back starts fresh at idle in neutral",
          "[physics][vehicles][inactive]")
{
    PhysicsApp test;
    Car car = DrivenCar(test);

    // Held still where it is, as for a cutscene.
    RigidBody& body = car.chassis.GetComponent<RigidBody>();
    body.motionType = MotionType::Kinematic;
    body.linearVelocity = Math::Vec3(0.0f);
    body.angularVelocity = Math::Vec3(0.0f);
    car.chassis.GetComponent<Vehicle>().input.forward = 0.0f;
    test.Run(1.0);
    INFO("kinematic, it is inactive with one warning and reads reset");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(WarningsAbout(test, "Vehicle on", car.chassis) == 1);
    RequireReset(StateOf(car));

    body.motionType = MotionType::Dynamic;
    test.Run(tick);
    INFO("dynamic again, it is at idle in neutral: the gear and engine speed it had were not carried over");
    REQUIRE(StateOf(car).gear == 0);
    REQUIRE(StateOf(car).engineRpm == Approx(idleRpm).margin(1.0f));
    test.Run(1.0);
    INFO("and it settles back onto its four wheels");
    REQUIRE(StateOf(car).wheelsOnGround == 4);
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("Each kind of inactive wheel warns once and is left out of its vehicle until valid again",
          "[physics][vehicles][inactive]")
{
    struct Case
    {
        const char* what;
        std::function<void(Entity& wheel, const Entity& floor)> invalidate;
        std::function<void(Entity& wheel)> restore;
    };
    const std::vector<Case> cases = {
        {"naming a body that is not a vehicle",
         [](Entity& wheel, const Entity& floor) { wheel.GetComponent<VehicleWheel>().vehicle = floor; }, nullptr},
        {"naming no entity", [](Entity& wheel, const Entity&) { wheel.GetComponent<VehicleWheel>().vehicle = {}; },
         nullptr},
        {"beside a RigidBody", [](Entity& wheel, const Entity&) { wheel.AddComponent<RigidBody>(); },
         [](Entity& wheel) { wheel.RemoveComponent<RigidBody>(); }},
        {"beside a CharacterBody", [](Entity& wheel, const Entity&) { wheel.AddComponent<CharacterBody>(); },
         [](Entity& wheel) { wheel.RemoveComponent<CharacterBody>(); }},
        {"beside a trigger Collider",
         [](Entity& wheel, const Entity&) {
             wheel.AddComponent<Collider>();
             wheel.AddComponent<Trigger>();
         },
         [](Entity& wheel) {
             wheel.RemoveComponent<Trigger>();
             wheel.RemoveComponent<Collider>();
         }},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        const Entity floor = AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        // A fifth wheel in the middle, so the car stands on its four corners without it.
        Entity middle = Place(test.Entities(), Math::Vec3(0.0f));
        middle.AddComponent<VehicleWheel>(
            VehicleWheel{.vehicle = car.chassis, .position = Math::Vec3(0.0f, wheelY, 0.0f)});
        test.Run(1.0);
        REQUIRE(StateOf(car).wheelsOnGround == 5);
        REQUIRE(test.warnings.empty());

        c.invalidate(middle, floor);
        test.Run(0.5);
        INFO(c.what << ": the wheel gave one warning, and the car stands on its other four");
        REQUIRE(WarningsAbout(test, "VehicleWheel on", middle) == 1);
        REQUIRE(WarningsAbout(test, "Vehicle on", car.chassis) == 0);
        REQUIRE(StateOf(car).wheelsOnGround == 4);
        RequireReset(StateOf(middle));

        if (c.restore)
        {
            c.restore(middle);
        }
        else
        {
            middle.GetComponent<VehicleWheel>().vehicle = car.chassis;
        }
        test.Run(0.5);
        INFO("valid again, it is one of the car's wheels, with no further warning");
        REQUIRE(StateOf(car).wheelsOnGround == 5);
        REQUIRE(WarningsAbout(test, "VehicleWheel on", middle) == 1);
    }
}

TEST_CASE("A wheel on its own chassis entity is inactive with one warning, and the vehicle drives on without it",
          "[physics][vehicles][inactive]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    car.chassis.AddComponent<VehicleWheel>(
        VehicleWheel{.vehicle = car.chassis, .position = Math::Vec3(0.0f, wheelY, 0.0f)});
    test.Run(1.0);
    INFO("one warning about the wheel, none about the vehicle, which stands on its four other wheels");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(WarningsAbout(test, "VehicleWheel on", car.chassis) == 1);
    REQUIRE(StateOf(car).wheelsOnGround == 4);
    REQUIRE_FALSE(StateOf(car.chassis).onGround);

    car.chassis.GetComponent<Vehicle>().input.forward = 0.2f;
    test.Run(2.0);
    INFO("and it drives off forward");
    REQUIRE(PositionOf(car.chassis).z < -1.0f);
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("A Joint naming a wheel entity names no body, and is inactive", "[physics][vehicles][inactive]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Entity box = AddBody(test.Entities(), Math::Vec3(5.0f, 10.0f, 0.0f));
    Entity joint = test.Entities().CreateEntity();
    joint.AddComponent<Joint>(
        Joint{.bodyA = box, .bodyB = car.wheels[0], .frameA = {}, .frameB = {}, .kind = BallJoint{}});
    test.Run(0.5);

    INFO("the joint gave one warning on going inactive");
    REQUIRE(WarningsAbout(test, "Joint on", joint) == 1);
    INFO("it holds nothing: the box fell freely, to y = " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y < 10.0f - 0.4f * 9.81f * 0.25f);
    INFO("and the car stands on its four wheels");
    REQUIRE(StateOf(car).wheelsOnGround == 4);
}
