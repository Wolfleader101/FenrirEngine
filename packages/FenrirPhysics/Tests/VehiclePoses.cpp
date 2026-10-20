// Wheel poses through the frame seam: physics writes each wheel entity's world position and rotation after the
// chassis's, its position raised by its compression, steered, and spun about its axle, X, and never its scale.

#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float poseMargin = 1e-4f;

    const Math::Vec3 axisX(1.0f, 0.0f, 0.0f);
    const Math::Vec3 axisY(0.0f, 1.0f, 0.0f);
    const Math::Vec3 forward(0.0f, 0.0f, -1.0f);

    // Where the wheel's centre is, in its chassis's axes from its origin.
    Math::Vec3 ChassisLocal(const Entity& chassis, const Entity& wheel)
    {
        const Transform at = GetWorldTransform(chassis);
        return Math::Conjugate(at.rot) * (PositionOf(wheel) - at.pos);
    }

    void RequireNear(const Math::Vec3& actual, const Math::Vec3& expected)
    {
        REQUIRE(actual.x == Approx(expected.x).margin(poseMargin));
        REQUIRE(actual.y == Approx(expected.y).margin(poseMargin));
        REQUIRE(actual.z == Approx(expected.z).margin(poseMargin));
    }
} // namespace

TEST_CASE("Before its wheels touch anything, each wheel entity sits at its position with the chassis's rotation",
          "[physics][vehicles][poses]")
{
    PhysicsApp test;
    const Math::Quat turned = Math::AngleAxis(0.7f, axisY);
    Car car = AddCar(test.Entities(), Math::Vec3(3.0f, 20.0f, -2.0f), turned);
    // Made somewhere else: physics places them.
    for (Entity& wheel : car.wheels)
    {
        wheel.GetComponent<Transform>().pos = Math::Vec3(0.0f);
    }
    test.app.RunFrame(tick);

    const Transform chassis = GetWorldTransform(car.chassis);
    for (const Entity& wheel : car.wheels)
    {
        const VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
        INFO("a falling wheel at x " << settings.position.x << ", z " << settings.position.z
                                     << " is at full extension, where its position says");
        RequireNear(ChassisLocal(car.chassis, wheel), settings.position);
        INFO("neither steered nor spun, it is turned as the chassis is");
        const Math::Quat rotation = GetWorldTransform(wheel).rot;
        RequireNear(rotation * axisX, chassis.rot * axisX);
        RequireNear(rotation * axisY, chassis.rot * axisY);
    }
}

TEST_CASE("A driven wheel spins about the chassis's X axis, its top rolling forward, raised by its compression",
          "[physics][vehicles][poses]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.2f; });
    test.Run(2.0);

    const Entity& rear = car.wheels[2];
    const Math::Vec3 upBefore = GetWorldTransform(rear).rot * axisY;
    const float spin = StateOf(rear).angularVelocity;
    test.app.RunFrame(tick);
    const Transform chassis = GetWorldTransform(car.chassis);
    const Transform wheel = GetWorldTransform(rear);

    INFO("its axle is the chassis's X");
    RequireNear(wheel.rot * axisX, chassis.rot * axisX);
    INFO("it sits under its position, raised by its compression (" << StateOf(rear).compression << " m)");
    const VehicleWheel& settings = rear.GetComponent<VehicleWheel>();
    RequireNear(ChassisLocal(car.chassis, rear), settings.position + axisY * StateOf(rear).compression);
    const Math::Vec3 upAfter = wheel.rot * axisY;
    INFO("over the tick its top turned forward by its spin times the tick (" << spin << " rad/s)");
    REQUIRE(Math::Dot(upAfter - upBefore, chassis.rot * forward) > 0.0f);
    const float turned = std::acos(std::clamp(Math::Dot(upAfter, upBefore), -1.0f, 1.0f));
    REQUIRE(turned == Approx(spin * static_cast<float>(tick)).epsilon(0.05));
}

TEST_CASE("A steered wheel is turned about the chassis's up by its steer angle", "[physics][vehicles][poses]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.steer = 1.0f; });
    test.Run(1.0);

    const Transform chassis = GetWorldTransform(car.chassis);
    const Transform front = GetWorldTransform(car.wheels[0]);
    const float steer = StateOf(car.wheels[0]).steerAngle;
    INFO("full right steer turns the front wheel's axle right about the chassis's up, by " << steer << " rad");
    RequireNear(front.rot * axisX, chassis.rot * (Math::AngleAxis(-steer, axisY) * axisX));
    REQUIRE(steer == Approx(frontSteer).margin(poseMargin));
}

TEST_CASE("Physics never writes a wheel entity's scale", "[physics][vehicles][poses]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    const Math::Vec3 look(0.1f, 0.6f, 0.6f);
    car.wheels[0].GetComponent<Transform>().scale = look;
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) {
        vehicle.input.forward = 0.5f;
        vehicle.input.steer = 0.5f;
    });
    test.Run(2.0);

    INFO("after two seconds posed by physics, the wheel entity keeps the scale game code gave it, exactly");
    const Math::Vec3 scale = car.wheels[0].GetComponent<Transform>().scale;
    REQUIRE(Exactly(scale.x, look.x));
    REQUIRE(Exactly(scale.y, look.y));
    REQUIRE(Exactly(scale.z, look.z));
}

TEST_CASE("Game code's writes to a wheel entity's Transform are overwritten by its pose", "[physics][vehicles][poses]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);

    const Entity wheel = car.wheels[1];
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[wheel](App&) {
                                      Transform& transform = wheel.GetComponent<Transform>();
                                      transform.pos = Math::Vec3(100.0f, 100.0f, 100.0f);
                                      transform.rot = Math::AngleAxis(1.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
                                  }});
    test.app.RunFrame(tick);

    INFO("a wheel moved and turned in Tick is back on its chassis after the step, posed as its state says");
    RequireNear(ChassisLocal(car.chassis, wheel),
                wheel.GetComponent<VehicleWheel>().position + axisY * StateOf(wheel).compression);
    RequireNear(GetWorldTransform(wheel).rot * axisX, GetWorldTransform(car.chassis).rot * axisX);
}
