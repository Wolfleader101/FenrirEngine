// Vehicles (ADR-0015) through the frame seam: the components' defaults, a default car driving, braking and steering,
// what physics writes back, and the vehicle's place among bodies, waking, rebuilds and worlds.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

TEST_CASE("Every vehicle component made with no arguments has the spec's defaults", "[physics][vehicles]")
{
    const Vehicle vehicle;
    INFO("a vehicle is a wheeled one, with no input, a cylinder probe, no tilt limit, enabled and an empty state");
    REQUIRE(vehicle.kind.Type() == VehicleType::Wheeled);
    REQUIRE(vehicle.kind.Is<WheeledVehicle>());
    REQUIRE(vehicle.probe == VehicleProbe::Cylinder);
    REQUIRE(Exactly(vehicle.maxTilt, std::numbers::pi_v<float>));
    REQUIRE(vehicle.enabled);

    const VehicleInput& input = vehicle.input;
    INFO("input is all zero but the track ratios, which are 1");
    REQUIRE(Exactly(input.forward, 0.0f));
    REQUIRE(Exactly(input.steer, 0.0f));
    REQUIRE(Exactly(input.brake, 0.0f));
    REQUIRE(Exactly(input.handbrake, 0.0f));
    REQUIRE(Exactly(input.leftTrack, 1.0f));
    REQUIRE(Exactly(input.rightTrack, 1.0f));
    REQUIRE(input.gear == 0);
    REQUIRE(Exactly(input.clutch, 0.0f));

    const WheeledVehicle& wheeled = vehicle.kind.As<WheeledVehicle>();
    const VehicleEngine& engine = wheeled.engine;
    INFO("the engine: 500 N m peaking at 4000 RPM, 1000 to 6000 RPM, 0.8 of the peak at each end");
    REQUIRE(Exactly(engine.maxTorque, 500.0f));
    REQUIRE(Exactly(engine.minRpm, 1000.0f));
    REQUIRE(Exactly(engine.maxRpm, 6000.0f));
    REQUIRE(Exactly(engine.peakTorqueRpm, 4000.0f));
    REQUIRE(Exactly(engine.torqueAtMinRpm, 0.8f));
    REQUIRE(Exactly(engine.torqueAtMaxRpm, 0.8f));
    REQUIRE(Exactly(engine.inertia, 0.5f));
    REQUIRE(Exactly(engine.angularDamping, 0.2f));

    const VehicleTransmission& transmission = wheeled.transmission;
    INFO("the gearbox: automatic, five gears from 2.66 to 0.74, reverse 2.90, shifting up at 4000 and down at 2000");
    REQUIRE(transmission.mode == TransmissionMode::Automatic);
    REQUIRE(transmission.gearCount == 5);
    REQUIRE(Exactly(transmission.firstGear, 2.66f));
    REQUIRE(Exactly(transmission.topGear, 0.74f));
    REQUIRE(Exactly(transmission.reverseGear, 2.90f));
    REQUIRE(Exactly(transmission.shiftUpRpm, 4000.0f));
    REQUIRE(Exactly(transmission.shiftDownRpm, 2000.0f));
    REQUIRE(Exactly(transmission.switchTime, 0.5f));
    REQUIRE(Exactly(transmission.clutchReleaseTime, 0.3f));
    REQUIRE(Exactly(transmission.switchLatency, 0.5f));
    REQUIRE(Exactly(transmission.clutchStrength, 10.0f));
    REQUIRE(Exactly(wheeled.limitedSlipRatio, 1.4f));

    const VehicleState state;
    INFO("a state reads all zero");
    REQUIRE(Exactly(state.forwardSpeed, 0.0f));
    REQUIRE(Exactly(state.engineRpm, 0.0f));
    REQUIRE(state.gear == 0);
    REQUIRE(Exactly(state.clutch, 0.0f));
    REQUIRE_FALSE(state.shifting);
    REQUIRE(state.wheelsOnGround == 0);
    REQUIRE(Exactly(state.leanAngle, 0.0f));
    REQUIRE(Exactly(state.leftTrackSpeed, 0.0f));
    REQUIRE(Exactly(state.rightTrackSpeed, 0.0f));

    const VehicleWheel wheel;
    INFO("a wheel names no vehicle, sits at the chassis origin on the left track, 0.3 m by 0.1 m");
    REQUIRE(wheel.vehicle == Entity());
    REQUIRE(wheel.position == Math::Vec3(0.0f));
    REQUIRE(wheel.track == VehicleTrackSide::Left);
    REQUIRE(Exactly(wheel.radius, 0.3f));
    REQUIRE(Exactly(wheel.width, 0.1f));
    REQUIRE(Exactly(wheel.inertia, 0.9f));
    REQUIRE(Exactly(wheel.angularDamping, 0.2f));
    INFO("with no caster, camber, toe or steering, and 1500 and 4000 N m of brake and handbrake");
    REQUIRE(Exactly(wheel.caster, 0.0f));
    REQUIRE(Exactly(wheel.camber, 0.0f));
    REQUIRE(Exactly(wheel.toe, 0.0f));
    REQUIRE(Exactly(wheel.maxSteerAngle, 0.0f));
    REQUIRE(Exactly(wheel.maxBrakeTorque, 1500.0f));
    REQUIRE(Exactly(wheel.maxHandbrakeTorque, 4000.0f));
    INFO("its suspension: 0.2 m of travel on a 1.5 Hz spring damped 0.5, no preload");
    REQUIRE(Exactly(wheel.suspension.travel, 0.2f));
    REQUIRE(Exactly(wheel.suspension.spring.frequency, 1.5f));
    REQUIRE(Exactly(wheel.suspension.spring.damping, 0.5f));
    REQUIRE(Exactly(wheel.suspension.preload, 0.0f));
    INFO("its tyre peaks at 1.2 at slip 0.06 along and 0.052 rad across, sliding at 1.0 past 0.2 and 0.35 rad");
    REQUIRE(Exactly(wheel.longitudinal.peakSlip, 0.06f));
    REQUIRE(Exactly(wheel.longitudinal.peakFriction, 1.2f));
    REQUIRE(Exactly(wheel.longitudinal.slideSlip, 0.2f));
    REQUIRE(Exactly(wheel.longitudinal.slideFriction, 1.0f));
    REQUIRE(Exactly(wheel.lateral.peakSlip, 0.052f));
    REQUIRE(Exactly(wheel.lateral.peakFriction, 1.2f));
    REQUIRE(Exactly(wheel.lateral.slideSlip, 0.35f));
    REQUIRE(Exactly(wheel.lateral.slideFriction, 1.0f));

    const VehicleWheelState& wheelState = wheel.state;
    INFO("a wheel's state is off the ground, naming nothing, all zero");
    REQUIRE_FALSE(wheelState.onGround);
    REQUIRE(wheelState.groundBody == Entity());
    REQUIRE(wheelState.groundCollider == Entity());
    REQUIRE(wheelState.contactPoint == Math::Vec3(0.0f));
    REQUIRE(wheelState.contactNormal == Math::Vec3(0.0f));
    REQUIRE(wheelState.groundVelocity == Math::Vec3(0.0f));
    REQUIRE(Exactly(wheelState.compression, 0.0f));
    REQUIRE(Exactly(wheelState.suspensionForce, 0.0f));
    REQUIRE(Exactly(wheelState.longitudinalSlip, 0.0f));
    REQUIRE(Exactly(wheelState.lateralSlip, 0.0f));
    REQUIRE(Exactly(wheelState.angularVelocity, 0.0f));
    REQUIRE(Exactly(wheelState.steerAngle, 0.0f));
}

TEST_CASE("The default car does 0 to 100 km/h in about the spike's 8.8 s, shifting up, and brakes to a stop",
          "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);

    std::vector<int> gears;
    float to100 = -1.0f;
    const int throttleTicks = 15 * 60;
    Drive(test.app, car.chassis, [&](int index, Vehicle& vehicle) {
        vehicle.input.forward = index < throttleTicks ? 1.0f : 0.0f;
        vehicle.input.brake = index < throttleTicks ? 0.0f : 1.0f;
        if (gears.empty() || gears.back() != vehicle.state.gear)
        {
            gears.push_back(vehicle.state.gear);
        }
        if (to100 < 0.0f && vehicle.state.forwardSpeed >= 100.0f * kmh)
        {
            to100 = static_cast<float>(index) / 60.0f;
        }
    });
    test.Run(15.0);

    INFO("full throttle on flat tarmac reaches 100 km/h within a second of the spike's 8.8 s (took " << to100 << " s)");
    REQUIRE(to100 > 7.8f);
    REQUIRE(to100 < 9.8f);
    INFO("the automatic gearbox goes up through its gears in order, from first to at least fourth");
    REQUIRE(gears.size() >= 4);
    REQUIRE(std::ranges::is_sorted(gears));
    REQUIRE(std::ranges::find(gears, 1) != gears.end());
    REQUIRE(gears.back() >= 4);

    test.Run(10.0);
    INFO("full brakes stop it (" << StateOf(car).forwardSpeed << " m/s)");
    REQUIRE(std::abs(StateOf(car).forwardSpeed) < 0.05f);
}

TEST_CASE("Only a wheel given a maxSteerAngle steers, by steer times its angle, and the car turns right",
          "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) {
        vehicle.input.forward = 0.5f;
        vehicle.input.steer = 1.0f;
    });
    test.Run(3.0);

    for (std::size_t i = 0; i < 2; ++i)
    {
        INFO("front wheel " << i << ", with a maxSteerAngle, is turned right by all of it");
        REQUIRE(StateOf(car.wheels[i]).steerAngle == Approx(frontSteer).margin(1e-4));
    }
    for (std::size_t i = 2; i < 4; ++i)
    {
        INFO("rear wheel " << i << ", with none, never steers");
        REQUIRE(Exactly(StateOf(car.wheels[i]).steerAngle, 0.0f));
    }
    INFO("steering right turns the car clockwise seen from above (yaw " << YawOf(car.chassis) << ")");
    REQUIRE(YawOf(car.chassis) < -0.5f);
}

TEST_CASE("Physics never writes a vehicle's input, out-of-range values included", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    const VehicleInput written{.forward = 2.0f,
                               .steer = -3.0f,
                               .brake = -1.0f,
                               .handbrake = 0.25f,
                               .leftTrack = 0.5f,
                               .rightTrack = -0.5f,
                               .gear = 7,
                               .clutch = 0.75f};
    car.chassis.GetComponent<Vehicle>().input = written;
    test.Run(2.0);

    const VehicleInput& read = car.chassis.GetComponent<Vehicle>().input;
    INFO("after driving on it for 2 s, the input reads back bit for bit as written");
    REQUIRE(std::memcmp(&read, &written, sizeof(VehicleInput)) == 0);
    INFO("and it drove: out-of-range input is clamped as physics applies it, not refused");
    REQUIRE(StateOf(car).forwardSpeed > 1.0f);
}

TEST_CASE("A car accelerating on a compound floor reads back each wheel's ground, contact, load and spin",
          "[physics][vehicles]")
{
    PhysicsApp test;
    // A compound static floor: its body is the parent's entity, its collider the child's.
    Entity floor = Place(test.Entities(), Math::Vec3(0.0f));
    floor.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Static});
    Entity slab = Place(test.Entities(), Math::Vec3(0.0f, -0.5f, 0.0f));
    slab.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(500.0f, 0.5f, 500.0f)}});
    floor.AddChild(slab);
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    // Gently, so every tyre grips: harder, the front tyres, unloaded as the car squats, spin.
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.2f; });
    test.Run(3.0);

    const VehicleState& state = StateOf(car);
    const VehicleTransmission transmission;
    const VehicleEngine engine;
    INFO("the car moves forward, its engine between idle and its top speed, in a forward gear, all wheels down");
    REQUIRE(state.forwardSpeed > 3.0f);
    REQUIRE(state.engineRpm >= engine.minRpm);
    REQUIRE(state.engineRpm <= engine.maxRpm);
    REQUIRE(state.gear >= 1);
    REQUIRE(state.gear <= transmission.gearCount);
    REQUIRE(state.clutch >= 0.0f);
    REQUIRE(state.clutch <= 1.0f);
    REQUIRE(state.wheelsOnGround == 4);
    INFO("a wheeled vehicle has no lean and no tracks");
    REQUIRE(Exactly(state.leanAngle, 0.0f));
    REQUIRE(Exactly(state.leftTrackSpeed, 0.0f));
    REQUIRE(Exactly(state.rightTrackSpeed, 0.0f));

    const Transform chassis = GetWorldTransform(car.chassis);
    const VehicleWheel defaults;
    float carried = 0.0f;
    for (const Entity& wheel : car.wheels)
    {
        const VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
        const VehicleWheelState& read = settings.state;
        const Math::Vec3 under = chassis.pos + chassis.rot * settings.position;
        INFO("wheel at x " << settings.position.x << ", z " << settings.position.z);
        INFO("it is on the ground, naming the floor's body and the slab's collider");
        REQUIRE(read.onGround);
        REQUIRE(read.groundBody == floor);
        REQUIRE(read.groundCollider == slab);
        INFO("its contact is on the floor under the wheel, facing up, on ground that does not move");
        REQUIRE(read.contactPoint.y == Approx(0.0f).margin(0.02));
        REQUIRE(read.contactPoint.x == Approx(under.x).margin(0.05));
        REQUIRE(read.contactNormal.y == Approx(1.0f).margin(1e-4));
        REQUIRE(read.groundVelocity == Math::Vec3(0.0f));
        INFO("its suspension is part way up its travel, carrying some of the car");
        REQUIRE(read.compression > 0.0f);
        REQUIRE(read.compression < defaults.suspension.travel);
        REQUIRE(read.suspensionForce > 0.0f);
        carried += read.suspensionForce;
        INFO("it rolls at the car's speed over its radius, driven a little faster than the ground");
        REQUIRE(read.angularVelocity == Approx(state.forwardSpeed / defaults.radius).epsilon(0.1));
        REQUIRE(read.longitudinalSlip > 0.0f);
        REQUIRE(read.longitudinalSlip < defaults.longitudinal.peakSlip);
        INFO("driving straight, it neither slips sideways nor steers");
        REQUIRE(std::abs(read.lateralSlip) < 0.01f);
        REQUIRE(Exactly(read.steerAngle, 0.0f));
    }
    INFO("together the four suspensions carry the car's weight (" << carried << " N)");
    REQUIRE(carried == Approx(carMass * 9.81f).epsilon(0.05));
}

TEST_CASE("A wheel's compression stays within its suspension travel over bumps", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    for (int i = 0; i < 10; ++i)
    {
        Entity bump = Place(test.Entities(), Math::Vec3(0.0f, 0.0f, -10.0f - 2.0f * static_cast<float>(i)));
        bump.GetComponent<Transform>().rot = Math::AngleAxis(0.25f * std::numbers::pi_v<float>, Math::Vec3(1, 0, 0));
        bump.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(4.0f, 0.1f, 0.1f)}});
    }
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.6f; });

    const float travel = VehicleSuspension{}.travel;
    float most = 0.0f;
    for (int frame = 0; frame < 8 * 60; ++frame)
    {
        test.app.RunFrame(tick);
        for (const Entity& wheel : car.wheels)
        {
            const float compression = StateOf(wheel).compression;
            INFO("frame " << frame << ": compression " << compression << " m is within 0 to " << travel);
            REQUIRE(compression >= 0.0f);
            REQUIRE(compression <= travel);
            most = std::max(most, compression);
        }
    }
    INFO("the car went over the bumps: the chassis is past them and some wheel rose well up its travel");
    REQUIRE(PositionOf(car.chassis).z < -30.0f);
    REQUIRE(most > 0.5f * travel);
}

TEST_CASE("A car in the air reads empty ground and zeroes for every wheel", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
    test.Run(0.5);

    INFO("half a second into a 20 m fall, no wheel is on the ground");
    REQUIRE(StateOf(car).wheelsOnGround == 0);
    for (const Entity& wheel : car.wheels)
    {
        const VehicleWheelState& read = StateOf(wheel);
        INFO("each wheel names no ground, and its contact, load and compression are zero");
        REQUIRE_FALSE(read.onGround);
        REQUIRE(read.groundBody == Entity());
        REQUIRE(read.groundCollider == Entity());
        REQUIRE(read.contactPoint == Math::Vec3(0.0f));
        REQUIRE(read.contactNormal == Math::Vec3(0.0f));
        REQUIRE(read.groundVelocity == Math::Vec3(0.0f));
        REQUIRE(Exactly(read.compression, 0.0f));
        REQUIRE(Exactly(read.suspensionForce, 0.0f));
    }
}

TEST_CASE("A car dropped from height lands on its bottomed-out wheels, its chassis kept off the floor",
          "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 8.0f, 0.0f));
    float lowest = PositionOf(car.chassis).y;
    for (int frame = 0; frame < 4 * 60; ++frame)
    {
        test.app.RunFrame(tick);
        lowest = std::min(lowest, PositionOf(car.chassis).y);
    }
    // Bottomed out, a wheel's centre is its travel above full extension, so the chassis's centre is at least its
    // radius plus the rest of its drop above the floor.
    const VehicleWheel wheel;
    const float bottomedOut = wheel.radius - (wheelY + wheel.suspension.travel);
    const float chassisHalfHeight = 0.25f;
    INFO("at its lowest the chassis's centre is " << lowest << " m up: the wheels' bottomed-out height " << bottomedOut
                                                  << " less the slop, and its box never touched");
    REQUIRE(lowest > bottomedOut - 0.05f);
    REQUIRE(lowest > chassisHalfHeight + 0.1f);
}

TEST_CASE("Driver input wakes a sleeping chassis, which then drives", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    const PhysicsWorld* world = nullptr;
    for (int frame = 0; frame < 20 * 60 && (world == nullptr || !world->IsSleeping(car.chassis)); ++frame)
    {
        test.app.RunFrame(tick);
        world = PhysicsWorld::Of(test.app.GetActiveScene());
    }
    INFO("left alone, the parked car falls asleep");
    REQUIRE(world->IsSleeping(car.chassis));
    const Math::Vec3 parked = PositionOf(car.chassis);

    car.chassis.GetComponent<Vehicle>().input.forward = 1.0f;
    test.app.RunFrame(tick);
    INFO("the tick after the throttle is pressed, the chassis is awake");
    REQUIRE_FALSE(world->IsSleeping(car.chassis));
    test.Run(2.0);
    INFO("and it drives off forward");
    REQUIRE(PositionOf(car.chassis).z < parked.z - 2.0f);
}

TEST_CASE("A vehicle with no wheels is valid and inert: a plain body that input does not move", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Entity chassis = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    chassis.AddComponent<Vehicle>().input.forward = 1.0f;
    test.Run(3.0);

    INFO("it falls and rests on the floor like any box, going nowhere");
    REQUIRE(PositionOf(chassis).y == Approx(0.5f).margin(restSlop));
    REQUIRE(std::abs(PositionOf(chassis).z) < 1e-3f);
    INFO("its state reads as the defaults, and nothing is warned about");
    REQUIRE(chassis.GetComponent<Vehicle>().state.wheelsOnGround == 0);
    REQUIRE(chassis.GetComponent<Vehicle>().state.engineRpm == 0.0f);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Adding and removing a wheel rebuilds the vehicle with its new wheel set", "[physics][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    // A middle pair as well, so the car stands on its four corners whichever middle wheel goes.
    std::vector<Entity> middle;
    for (const float x : {-wheelX, wheelX})
    {
        middle.push_back(Place(test.Entities(), Math::Vec3(0.0f)));
        middle.back().AddComponent<VehicleWheel>(
            VehicleWheel{.vehicle = car.chassis, .position = Math::Vec3(x, wheelY, 0.0f)});
    }
    test.Run(1.0);
    REQUIRE(StateOf(car).wheelsOnGround == 6);

    middle[0].RemoveComponent<VehicleWheel>();
    test.Run(0.5);
    INFO("with one wheel's VehicleWheel removed, five wheels are on the ground");
    REQUIRE(StateOf(car).wheelsOnGround == 5);

    test.Entities().DestroyEntity(middle[1]);
    test.Run(0.5);
    INFO("with a second wheel's entity destroyed, four");
    REQUIRE(StateOf(car).wheelsOnGround == 4);

    Entity added = Place(test.Entities(), Math::Vec3(0.0f));
    added.AddComponent<VehicleWheel>(VehicleWheel{.vehicle = car.chassis, .position = Math::Vec3(0.0f, wheelY, 0.0f)});
    test.Run(1.0);
    INFO("with a new wheel added in the middle, five, the new one on the ground");
    REQUIRE(StateOf(car).wheelsOnGround == 5);
    REQUIRE(StateOf(added).onGround);
}

namespace
{
    // When the vehicles turn, once into the bumps.
    constexpr int turnAt = 480;
    // Poses recorded each tick: the car's chassis first, then its four wheels, then the motorcycle's and the tank's.
    constexpr std::size_t posesPerTick = (1 + 4) + (1 + 2) + (1 + 2 * tankWheelsASide);

    // A car, a motorcycle and a tank side by side over bumps, past a row of crates and into a turn: each chassis's
    // and every wheel's world pose after each tick.
    std::vector<Transform> DriveRecorded(const PhysicsConfig& config)
    {
        PhysicsApp test(config);
        AddFloor(test.Entities());
        for (int i = 0; i < 10; ++i)
        {
            Entity bump = Place(test.Entities(), Math::Vec3(0.0f, 0.0f, -10.0f - 3.0f * static_cast<float>(i)));
            bump.GetComponent<Transform>().rot = Math::AngleAxis(0.785f, Math::Vec3(1.0f, 0.0f, 0.0f));
            bump.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(12.0f, 0.1f, 0.1f)}});
        }
        for (int i = 0; i < 12; ++i)
        {
            Entity crate = Place(test.Entities(), Math::Vec3(-11.0f + 2.0f * static_cast<float>(i), 0.3f, -45.0f));
            crate.AddComponent<RigidBody>(RigidBody{.mass = 20.0f});
            crate.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.3f)}});
        }
        const std::vector<Car> vehicles = {AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f)),
                                           AddMotorcycle(test.Entities(), Math::Vec3(-6.0f, 1.0f, 0.0f)),
                                           AddTank(test.Entities(), Math::Vec3(7.0f, 1.5f, 0.0f))};
        // At part throttle: flat out, the bumps throw the car about, so where it gets to differs between platforms.
        Drive(test.app, vehicles[0].chassis, [](int index, Vehicle& vehicle) {
            vehicle.input.forward = 0.6f;
            vehicle.input.steer = index > turnAt ? 0.6f : 0.0f;
        });
        Drive(test.app, vehicles[1].chassis, [](int index, Vehicle& vehicle) {
            vehicle.input.forward = 0.4f;
            vehicle.input.steer = index > turnAt ? -0.3f : 0.0f;
        });
        Drive(test.app, vehicles[2].chassis, [](int index, Vehicle& vehicle) {
            vehicle.input.forward = 0.6f;
            vehicle.input.rightTrack = index > turnAt ? 0.6f : 1.0f;
        });
        std::vector<Transform> poses;
        for (int i = 0; i < 600; ++i)
        {
            test.app.RunFrame(tick);
            for (const Car& vehicle : vehicles)
            {
                poses.push_back(GetWorldTransform(vehicle.chassis));
                for (const Entity& wheel : vehicle.wheels)
                {
                    poses.push_back(GetWorldTransform(wheel));
                }
            }
        }
        return poses;
    }

    bool BitIdentical(const std::vector<Transform>& a, const std::vector<Transform>& b)
    {
        return a.size() == b.size() && std::ranges::equal(a, b, [](const Transform& x, const Transform& y) {
                   return std::memcmp(&x.pos, &y.pos, sizeof(x.pos)) == 0 &&
                          std::memcmp(&x.rot, &y.rot, sizeof(x.rot)) == 0;
               });
    }
} // namespace

TEST_CASE("Two drives of a car, a motorcycle and a tank over bumps and crates are bit-identical over 600 ticks, "
          "threaded or not",
          "[physics][vehicles]")
{
    PhysicsConfig threaded = StepPosesOnly();
    PhysicsConfig single = StepPosesOnly();
    single.useWorkerThreads = false;
    const std::vector<Transform> first = DriveRecorded(threaded);
    INFO("the spike's 16 200 poses");
    REQUIRE(first.size() == 600 * posesPerTick);

    // The bumps run from z = -10 to -37.
    const float beforeTurning = first[posesPerTick * turnAt].pos.z;
    INFO("the car was well into the bumps before it turned (it was at z = " << beforeTurning << ")");
    REQUIRE(beforeTurning < -20.0f);
    INFO("a second threaded run gives every chassis and wheel pose, bit for bit");
    REQUIRE(BitIdentical(first, DriveRecorded(threaded)));
    INFO("and so does a run on one thread");
    REQUIRE(BitIdentical(first, DriveRecorded(single)));
}

TEST_CASE("A paused world's vehicle neither moves nor changes its state, and drives on when resumed",
          "[physics][vehicles]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    car.chassis.GetComponent<Vehicle>().input.forward = 1.0f;
    test.Run(2.0);
    const Transform chassis = GetWorldTransform(car.chassis);
    const Transform wheel = GetWorldTransform(car.wheels[0]);
    const VehicleState state = StateOf(car);

    test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    test.Run(1.0);
    const Transform chassisNow = GetWorldTransform(car.chassis);
    const Transform wheelNow = GetWorldTransform(car.wheels[0]);
    INFO("while its scene is inactive, the chassis and its wheels stay put and the state is kept");
    REQUIRE(std::memcmp(&chassisNow.pos, &chassis.pos, sizeof(chassis.pos)) == 0);
    REQUIRE(std::memcmp(&wheelNow.rot, &wheel.rot, sizeof(wheel.rot)) == 0);
    REQUIRE(std::memcmp(&StateOf(car), &state, sizeof(VehicleState)) == 0);

    test.app.ChangeActiveScene(first);
    test.Run(0.5);
    INFO("active again, it drives on");
    REQUIRE(PositionOf(car.chassis).z < chassis.pos.z - 1.0f);
}

TEST_CASE("Destroying a scene with a vehicle in it ends its world cleanly", "[physics][vehicles]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    Scene& other = test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    AddFloor(other.GetEntityList());
    Car car = AddCar(other.GetEntityList(), Math::Vec3(0.0f, 1.0f, 0.0f));
    car.chassis.GetComponent<Vehicle>().input.forward = 1.0f;
    test.Run(1.0);
    REQUIRE(StateOf(car).wheelsOnGround == 4);

    test.app.ChangeActiveScene(first);
    test.app.DestroyScene("Other");
    AddFloor(test.Entities());
    Car another = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    INFO("the remaining scene's vehicle drives on its own world, with no error logged");
    REQUIRE(StateOf(another).wheelsOnGround == 4);
    REQUIRE(test.errors.empty());
}
