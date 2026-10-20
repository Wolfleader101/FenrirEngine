// Vehicle settings as live edits through the frame seam: suspension, wheel size, steering and brakes changed while a
// car drives, the per-wheel angles in the wheel poses, the probe and the tilt limit, out-of-range wheel values, and
// every edit waking the chassis.

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
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
    constexpr float poseMargin = 1e-4f;
    constexpr float pi = std::numbers::pi_v<float>;

    const Math::Vec3 axisX(1.0f, 0.0f, 0.0f);
    const Math::Vec3 axisY(0.0f, 1.0f, 0.0f);

    // Where the wheel's centre is, and a direction of it, in its chassis's axes.
    Math::Vec3 ChassisLocal(const Entity& chassis, const Entity& wheel)
    {
        const Transform at = GetWorldTransform(chassis);
        return Math::Conjugate(at.rot) * (PositionOf(wheel) - at.pos);
    }

    Math::Vec3 ChassisLocalAxle(const Entity& chassis, const Entity& wheel)
    {
        return Math::Conjugate(GetWorldTransform(chassis).rot) * (GetWorldTransform(wheel).rot * axisX);
    }

    // How far the chassis's up is turned from world up: 1 upright, 0 on its side, -1 on its roof.
    float UprightOf(const Entity& chassis)
    {
        return (GetWorldTransform(chassis).rot * axisY).y;
    }

    void EachWheel(Car& car, const std::function<void(VehicleWheel&)>& edit)
    {
        for (Entity& wheel : car.wheels)
        {
            edit(wheel.GetComponent<VehicleWheel>());
        }
    }

    // A car on the floor, left to settle.
    struct SettledCar
    {
        PhysicsApp test;
        Car car;

        SettledCar()
        {
            AddFloor(test.Entities());
            car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
            test.Run(2.0);
        }
    };
} // namespace

TEST_CASE("Softer springs lower a car's chassis and bigger wheels raise it, as live edits", "[physics][vehicles]")
{
    SettledCar settled;
    Car& car = settled.car;
    const float stock = PositionOf(car.chassis).y;

    EachWheel(car, [](VehicleWheel& wheel) { wheel.suspension.spring = {.frequency = 1.2f, .damping = 0.5f}; });
    settled.test.Run(3.0);
    const float soft = PositionOf(car.chassis).y;
    INFO("on 1.2 Hz springs instead of 1.5 Hz the chassis sits lower (" << stock << " m, then " << soft << " m)");
    REQUIRE(soft < stock - 0.03f);

    EachWheel(car, [](VehicleWheel& wheel) { wheel.radius = 0.4f; });
    settled.test.Run(3.0);
    const float big = PositionOf(car.chassis).y;
    INFO("on wheels 0.1 m bigger it sits about 0.1 m higher (" << big << " m)");
    REQUIRE(big - soft == Approx(0.1f).margin(0.02f));
    REQUIRE(StateOf(car).wheelsOnGround == 4);
}

TEST_CASE("A new maxSteerAngle steers on the next tick", "[physics][vehicles]")
{
    SettledCar settled;
    Car& car = settled.car;
    car.chassis.GetComponent<Vehicle>().input = VehicleInput{.forward = 0.2f, .steer = 1.0f};
    settled.test.Run(1.0);
    REQUIRE(StateOf(car.wheels[0]).steerAngle == Approx(frontSteer).margin(1e-4));

    car.wheels[0].GetComponent<VehicleWheel>().maxSteerAngle = 0.2f;
    car.wheels[2].GetComponent<VehicleWheel>().maxSteerAngle = -0.3f;
    settled.test.app.RunFrame(tick);
    INFO("the tick after the edit, the front left wheel steers by its new angle, the other front one by its old");
    REQUIRE(StateOf(car.wheels[0]).steerAngle == Approx(0.2f).margin(1e-4));
    REQUIRE(StateOf(car.wheels[1]).steerAngle == Approx(frontSteer).margin(1e-4));
    INFO("and a rear wheel given a negative angle steers the other way");
    REQUIRE(StateOf(car.wheels[2]).steerAngle == Approx(-0.3f).margin(1e-4));
}

namespace
{
    // Seconds a car braking from about 14 m/s takes to stop, its brakes given `brakeTorque` as it starts braking.
    float SecondsToStop(float brakeTorque)
    {
        SettledCar settled;
        Car& car = settled.car;
        car.chassis.GetComponent<Vehicle>().input.forward = 0.5f;
        settled.test.Run(4.0);
        REQUIRE(StateOf(car).forwardSpeed > 8.0f);

        car.chassis.GetComponent<Vehicle>().input = VehicleInput{.brake = 1.0f};
        EachWheel(car, [brakeTorque](VehicleWheel& wheel) { wheel.maxBrakeTorque = brakeTorque; });
        int ticks = 0;
        for (; ticks < 30 * 60 && StateOf(car).forwardSpeed > 0.1f; ++ticks)
        {
            settled.test.app.RunFrame(tick);
        }
        return static_cast<float>(ticks) / 60.0f;
    }
} // namespace

TEST_CASE("A higher maxBrakeTorque stops a car sooner", "[physics][vehicles]")
{
    const float weak = SecondsToStop(400.0f);
    const float stock = SecondsToStop(1500.0f);
    INFO("400 N m brakes stop it in " << weak << " s, 1500 N m ones in " << stock << " s");
    REQUIRE(stock < 0.75f * weak);
    REQUIRE(weak < 30.0f);
}

TEST_CASE("The handbrake locks the wheels that have a maxHandbrakeTorque, and only those", "[physics][vehicles]")
{
    SettledCar settled;
    Car& car = settled.car;
    car.chassis.GetComponent<Vehicle>().input.forward = 0.4f;
    settled.test.Run(3.0);

    // Live: the front wheels lose their handbrake as it is pulled.
    for (std::size_t i = 0; i < 2; ++i)
    {
        car.wheels[i].GetComponent<VehicleWheel>().maxHandbrakeTorque = 0.0f;
    }
    car.chassis.GetComponent<Vehicle>().input = VehicleInput{.handbrake = 1.0f};
    settled.test.Run(0.5);

    const float speed = StateOf(car).forwardSpeed;
    REQUIRE(speed > 2.0f);
    for (std::size_t i = 0; i < 2; ++i)
    {
        INFO("front wheel " << i << ", with no handbrake torque, rolls on at the car's speed (" << speed << " m/s)");
        REQUIRE(StateOf(car.wheels[i]).angularVelocity == Approx(speed / VehicleWheel{}.radius).epsilon(0.1));
    }
    for (std::size_t i = 2; i < 4; ++i)
    {
        INFO("rear wheel " << i << ", with the default 4000 N m, is locked");
        REQUIRE(std::abs(StateOf(car.wheels[i]).angularVelocity) < 0.05f);
    }
}

TEST_CASE("Camber and toe turn each wheel's axle, mirrored by side and not at x = 0, and caster tilts its travel back",
          "[physics][vehicles][poses]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    // A fifth wheel on the middle line.
    Entity middle = Place(test.Entities(), Math::Vec3(0.0f));
    middle.AddComponent<VehicleWheel>(VehicleWheel{.vehicle = car.chassis, .position = Math::Vec3(0.0f, wheelY, 0.0f)});
    car.wheels.push_back(middle);
    test.Run(2.0);

    const float camber = 0.1f;
    EachWheel(car, [camber](VehicleWheel& wheel) { wheel.camber = camber; });
    test.app.RunFrame(tick);
    for (const Entity& wheel : car.wheels)
    {
        const float x = wheel.GetComponent<VehicleWheel>().position.x;
        const float outwards = x < 0.0f ? -1.0f : 1.0f;
        const Math::Vec3 axle = ChassisLocalAxle(car.chassis, wheel);
        INFO("with camber, the wheel at x " << x << " has its top tilted outwards: its axle dips on the outside");
        REQUIRE(axle.x == Approx(std::cos(camber)).margin(poseMargin));
        REQUIRE(axle.y * outwards == Approx(-std::sin(camber)).margin(poseMargin));
        REQUIRE(axle.z == Approx(0.0f).margin(poseMargin));
    }

    const float toe = 0.05f;
    EachWheel(car, [toe](VehicleWheel& wheel) {
        wheel.camber = 0.0f;
        wheel.toe = toe;
    });
    test.app.RunFrame(tick);
    for (const Entity& wheel : car.wheels)
    {
        const float x = wheel.GetComponent<VehicleWheel>().position.x;
        const float outwards = x < 0.0f ? -1.0f : 1.0f;
        const Math::Vec3 axle = ChassisLocalAxle(car.chassis, wheel);
        INFO("with toe, the wheel at x " << x << " has its front turned inwards: its axle's outer end turns forward");
        REQUIRE(axle.x == Approx(std::cos(toe)).margin(poseMargin));
        REQUIRE(axle.y == Approx(0.0f).margin(poseMargin));
        REQUIRE(axle.z * outwards == Approx(-std::sin(toe)).margin(poseMargin));
    }

    const float caster = 0.3f;
    EachWheel(car, [caster](VehicleWheel& wheel) {
        wheel.toe = 0.0f;
        wheel.caster = caster;
    });
    test.Run(1.0);
    for (const Entity& wheel : car.wheels)
    {
        const VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
        const float compression = settings.state.compression;
        const Math::Vec3 local = ChassisLocal(car.chassis, wheel);
        INFO("with caster, the wheel at x " << settings.position.x << " rose " << compression
                                            << " m up a steering axis tilted back, on either side");
        REQUIRE(compression > 0.05f);
        REQUIRE(local.x == Approx(settings.position.x).margin(poseMargin));
        REQUIRE(local.y - settings.position.y == Approx(std::cos(caster) * compression).margin(poseMargin));
        REQUIRE(local.z - settings.position.z == Approx(std::sin(caster) * compression).margin(poseMargin));
        INFO("unsteered, its axle stays the chassis's X");
        const Math::Vec3 axle = ChassisLocalAxle(car.chassis, wheel);
        REQUIRE(axle.x == Approx(1.0f).margin(poseMargin));
    }
}

TEST_CASE("A steered wheel with caster turns about its tilted steering axis", "[physics][vehicles][poses]")
{
    SettledCar settled;
    Car& car = settled.car;
    const float caster = 0.3f;
    EachWheel(car, [caster](VehicleWheel& wheel) { wheel.caster = caster; });
    car.chassis.GetComponent<Vehicle>().input.steer = 1.0f;
    settled.test.Run(1.0);

    // Steering about an axis tilted back by the caster turns the axle out of the chassis's level plane.
    const Math::Vec3 steeringAxis = Math::AngleAxis(caster, axisX) * axisY;
    const Math::Vec3 expected = Math::AngleAxis(-frontSteer, steeringAxis) * axisX;
    for (std::size_t i = 0; i < 2; ++i)
    {
        INFO("front wheel " << i << ", steered right by " << frontSteer << " rad");
        const Math::Vec3 axle = ChassisLocalAxle(car.chassis, car.wheels[i]);
        REQUIRE(axle.x == Approx(expected.x).margin(poseMargin));
        REQUIRE(axle.y == Approx(expected.y).margin(poseMargin));
        REQUIRE(axle.z == Approx(expected.z).margin(poseMargin));
    }
}

namespace
{
    // A car whose wheels sit high on a low chassis, its front wheels just ahead of its nose, driven slowly at a kerb
    // taller than the chassis is off the ground. Its probe starts as `probe` and is `then` after a second.
    Math::Vec3 AtKerb(VehicleProbe probe, VehicleProbe then)
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        // From z = -8 on, 0.3 m high.
        Entity kerb = Place(test.Entities(), Math::Vec3(0.0f, 0.15f, -108.0f));
        kerb.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(5.0f, 0.15f, 100.0f)}});
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        car.chassis.GetComponent<Collider>().shape = BoxShape{Math::Vec3(0.9f, 0.32f, 1.45f)};
        EachWheel(car, [](VehicleWheel& wheel) { wheel.position.y = -0.3f; });
        car.chassis.GetComponent<Vehicle>().probe = probe;
        test.Run(1.0);
        car.chassis.GetComponent<Vehicle>().probe = then;
        car.chassis.GetComponent<Vehicle>().input.forward = 0.3f;
        test.Run(10.0);
        return PositionOf(car.chassis);
    }
} // namespace

TEST_CASE("Every probe drives a car on flat ground at the same ride height", "[physics][vehicles]")
{
    float height = 0.0f;
    for (const VehicleProbe probe : {VehicleProbe::Cylinder, VehicleProbe::Ray, VehicleProbe::Sphere})
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        car.chassis.GetComponent<Vehicle>().probe = probe;
        test.Run(1.0);
        car.chassis.GetComponent<Vehicle>().input.forward = 0.2f;
        test.Run(3.0);

        INFO("probe " << static_cast<int>(probe) << ": the car drives on all four wheels");
        REQUIRE(StateOf(car).wheelsOnGround == 4);
        REQUIRE(StateOf(car).forwardSpeed > 3.0f);
        if (probe == VehicleProbe::Cylinder)
        {
            height = PositionOf(car.chassis).y;
        }
        INFO("at the cylinder's ride height, " << height << " m (" << PositionOf(car.chassis).y << " m)");
        REQUIRE(PositionOf(car.chassis).y == Approx(height).margin(0.01f));
    }
}

TEST_CASE("A cylinder probe rolls a car over a kerb a ray probe catches on, and a live probe change takes effect",
          "[physics][vehicles]")
{
    const Math::Vec3 cylinder = AtKerb(VehicleProbe::Cylinder, VehicleProbe::Cylinder);
    INFO("with the cylinder the car climbs the kerb and drives on along its top (" << cylinder.z << ", " << cylinder.y
                                                                                   << ")");
    REQUIRE(cylinder.z < -12.0f);
    REQUIRE(cylinder.y > 0.6f);

    const Math::Vec3 ray = AtKerb(VehicleProbe::Ray, VehicleProbe::Ray);
    INFO("with a ray its nose meets the kerb before the ray finds it, and it stops there (" << ray.z << ")");
    REQUIRE(ray.z > -7.0f);

    const Math::Vec3 changed = AtKerb(VehicleProbe::Cylinder, VehicleProbe::Ray);
    INFO("a car made with a cylinder and changed to a ray before it gets there catches as the ray did (" << changed.z
                                                                                                         << ")");
    REQUIRE(changed.z > -7.0f);
}

TEST_CASE("A live probe change keeps the vehicle: its engine and wheels carry on", "[physics][vehicles]")
{
    SettledCar settled;
    Car& car = settled.car;
    car.chassis.GetComponent<Vehicle>().input.forward = 0.3f;
    settled.test.Run(2.0);
    const float rpm = StateOf(car).engineRpm;
    const float spin = StateOf(car.wheels[2]).angularVelocity;

    car.chassis.GetComponent<Vehicle>().probe = VehicleProbe::Sphere;
    settled.test.app.RunFrame(tick);
    INFO("the tick after the change the engine (" << rpm << " RPM) and the wheels (" << spin
                                                  << " rad/s) are where they were, not restarted");
    REQUIRE(StateOf(car).engineRpm == Approx(rpm).epsilon(0.05));
    REQUIRE(StateOf(car.wheels[2]).angularVelocity == Approx(spin).epsilon(0.05));
    REQUIRE(StateOf(car).wheelsOnGround == 4);
}

TEST_CASE("A chassis moved to another layer probes with it", "[physics][vehicles]")
{
    PhysicsApp test;
    Entity floor = AddFloor(test.Entities());
    floor.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 1});
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    REQUIRE(StateOf(car).wheelsOnGround == 4);

    // The floor's layer is no longer one the chassis collides with.
    car.chassis.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u, .mask = 1u});
    test.app.RunFrame(tick);
    INFO("the next step, no wheel finds the floor");
    REQUIRE(StateOf(car).wheelsOnGround == 0);
}

namespace
{
    // The least the chassis's up came to world up over 4 s after it is thrown up and rolled hard to the right, with
    // the tilt limit set live to `maxTilt`.
    float LeastUprightAfterKick(float maxTilt)
    {
        SettledCar settled;
        Car& car = settled.car;
        car.chassis.GetComponent<Vehicle>().maxTilt = maxTilt;
        settled.test.app.RunFrame(tick);
        // Off the ground: on it, the wheels on the low side stop the roll.
        PhysicsWorld* world = PhysicsWorld::Of(settled.test.app.GetActiveScene());
        world->AddImpulse(car.chassis, Math::Vec3(0.0f, 5.0f * carMass, 0.0f));
        world->AddAngularImpulse(car.chassis, Math::Vec3(0.0f, 0.0f, -2000.0f));
        float least = 1.0f;
        for (int frame = 0; frame < 4 * 60; ++frame)
        {
            settled.test.app.RunFrame(tick);
            least = std::min(least, UprightOf(car.chassis));
        }
        return least;
    }
} // namespace

TEST_CASE("A 60 degree maxTilt stops a roll that a kicked car otherwise makes", "[physics][vehicles]")
{
    const float free = LeastUprightAfterKick(pi);
    INFO("with no limit, the throw rolls it past its side (its up came to " << free << ")");
    REQUIRE(free < 0.0f);

    const float limited = LeastUprightAfterKick(pi / 3.0f);
    INFO("with 60 degrees it stays within about 60 degrees of upright (its up came to " << limited << ")");
    REQUIRE(limited > 0.45f);
}

namespace
{
    struct OutOfRange
    {
        const char* name;
        std::function<void(VehicleWheel&)> set;
    };
} // namespace

TEST_CASE("Each out-of-range wheel value is corrected with one warning per wheel", "[physics][vehicles]")
{
    const std::vector<OutOfRange> cases = {
        {"a negative width", [](VehicleWheel& w) { w.width = -0.1f; }},
        {"a zero radius", [](VehicleWheel& w) { w.radius = 0.0f; }},
        {"a negative inertia", [](VehicleWheel& w) { w.inertia = -1.0f; }},
        {"a negative angular damping", [](VehicleWheel& w) { w.angularDamping = -1.0f; }},
        {"a negative travel", [](VehicleWheel& w) { w.suspension.travel = -0.1f; }},
        {"a negative spring frequency", [](VehicleWheel& w) { w.suspension.spring.frequency = -1.0f; }},
        {"a negative brake torque", [](VehicleWheel& w) { w.maxBrakeTorque = -1.0f; }},
        {"a negative handbrake torque", [](VehicleWheel& w) { w.maxHandbrakeTorque = -1.0f; }},
        {"a quarter-turn maxSteerAngle", [](VehicleWheel& w) { w.maxSteerAngle = 2.0f; }},
        {"a quarter-turn caster", [](VehicleWheel& w) { w.caster = -2.0f; }},
        {"a quarter-turn camber", [](VehicleWheel& w) { w.camber = 2.0f; }},
        {"a quarter-turn toe", [](VehicleWheel& w) { w.toe = 2.0f; }},
        {"a negative tyre friction", [](VehicleWheel& w) { w.lateral.peakFriction = -1.0f; }},
        {"a slide slip before the peak", [](VehicleWheel& w) { w.longitudinal.slideSlip = 0.01f; }},
    };
    for (const OutOfRange& bad : cases)
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        car.chassis.GetComponent<Vehicle>().input = VehicleInput{.forward = 0.2f, .steer = 0.5f};
        test.Run(0.5);
        REQUIRE(test.warnings.empty());

        // Two wheels, one from the start of a tick and one a little later, kept out of range.
        bad.set(car.wheels[0].GetComponent<VehicleWheel>());
        test.Run(0.5);
        bad.set(car.wheels[3].GetComponent<VehicleWheel>());
        test.Run(1.0);

        INFO(bad.name << " on two wheels: " << test.warnings.size() << " warnings");
        REQUIRE(test.warnings.size() == 2);
        REQUIRE(test.warnings[0].find("entity " + std::to_string(car.wheels[0].GetId())) != std::string::npos);
        REQUIRE(test.warnings[1].find("entity " + std::to_string(car.wheels[3].GetId())) != std::string::npos);
        INFO("the car drives on, with nothing broken");
        REQUIRE(test.errors.empty());
        REQUIRE(std::isfinite(PositionOf(car.chassis).y));
        REQUIRE(PositionOf(car.chassis).y > 0.2f);
    }
}

TEST_CASE("Out-of-range wheel values are corrected as the spec says", "[physics][vehicles]")
{
    SettledCar settled;
    Car& car = settled.car;
    car.wheels[0].GetComponent<VehicleWheel>().maxSteerAngle = 3.0f;
    car.wheels[2].GetComponent<VehicleWheel>().suspension.travel = -1.0f;
    car.chassis.GetComponent<Vehicle>().input.steer = 1.0f;
    settled.test.Run(1.0);
    INFO("a maxSteerAngle past a quarter turn steers just short of one");
    REQUIRE(StateOf(car.wheels[0]).steerAngle == Approx(0.5f * pi).margin(1e-3));
    REQUIRE(StateOf(car.wheels[0]).steerAngle < 0.5f * pi);
    INFO("a negative travel is no travel: the suspension is rigid");
    REQUIRE(Exactly(StateOf(car.wheels[2]).compression, 0.0f));
    REQUIRE(StateOf(car.wheels[2]).onGround);
}

TEST_CASE("Every wheel, probe and tilt edit wakes a sleeping chassis", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.app.RunFrame(tick);
    const PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());

    const std::vector<std::pair<const char*, std::function<void()>>> edits = {
        {"a spring", [&] { car.wheels[1].GetComponent<VehicleWheel>().suspension.spring.frequency = 1.4f; }},
        {"a radius", [&] { car.wheels[2].GetComponent<VehicleWheel>().radius = 0.31f; }},
        {"a camber", [&] { car.wheels[3].GetComponent<VehicleWheel>().camber = 0.01f; }},
        {"the probe", [&] { car.chassis.GetComponent<Vehicle>().probe = VehicleProbe::Ray; }},
        {"the tilt limit", [&] { car.chassis.GetComponent<Vehicle>().maxTilt = 1.0f; }},
    };
    for (const auto& [name, edit] : edits)
    {
        for (int frame = 0; frame < 20 * 60 && !world->IsSleeping(car.chassis); ++frame)
        {
            test.app.RunFrame(tick);
        }
        REQUIRE(world->IsSleeping(car.chassis));
        edit();
        test.app.RunFrame(tick);
        INFO("the tick after " << name << " is edited, the chassis is awake");
        REQUIRE_FALSE(world->IsSleeping(car.chassis));
    }
}
