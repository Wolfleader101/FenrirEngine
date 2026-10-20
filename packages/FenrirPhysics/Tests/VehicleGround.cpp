// What is ground for a wheel, how much it grips, and how vehicles meet other bodies: probes filtered by the chassis's
// CollisionFilter and never finding triggers, the chassis or a character; grip from the ground's material; wheels
// pushing dynamic ground and riding kinematic ground; characters met by the chassis; queries finding only the chassis.

#include <algorithm>
#include <cmath>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float dt = static_cast<float>(tick);

    // Whether any of the car's wheels stands on `body`.
    bool AnyWheelOn(const Car& car, const Entity& body)
    {
        return std::ranges::any_of(car.wheels, [&body](const Entity& wheel) {
            return StateOf(wheel).onGround && StateOf(wheel).groundBody == body;
        });
    }

    // A car dropped onto a floor in layer 1 with `floorMask`, its chassis filtered by `chassis`: how many of its
    // wheels find the floor once it has settled.
    int WheelsOnLayeredFloor(std::uint32_t floorMask, const CollisionFilter& chassis)
    {
        PhysicsApp test;
        Entity floor = AddFloor(test.Entities());
        floor.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 1, .mask = floorMask});
        // A second floor below that the chassis always collides with, so a car whose wheels miss the first rests here.
        Entity under = Place(test.Entities(), Math::Vec3(0.0f, -5.5f, 0.0f));
        under.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(50.0f, 0.5f, 50.0f)}});
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        car.chassis.AddComponent<CollisionFilter>(chassis);
        test.Run(1.0);
        return static_cast<int>(std::ranges::count_if(
            car.wheels, [&floor](const Entity& wheel) { return StateOf(wheel).groundBody == floor; }));
    }

    struct GripRun
    {
        float speed = 0.0f;
        // The largest longitudinal slip of any wheel over the last second of the drive: pulling away from rest it is
        // briefly huge, the ground speed under the wheel near 0.
        float slip = 0.0f;
        // Metres from releasing the throttle and braking hard to standing still.
        float braking = 0.0f;
    };

    // The spike's grip run: full throttle for 5 s on a floor of `friction`, then full brake until the car stops.
    GripRun DriveOn(float friction)
    {
        PhysicsApp test;
        AddFloor(test.Entities(), friction);
        Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        test.Run(1.0);
        GripRun run;
        Vehicle& vehicle = car.chassis.GetComponent<Vehicle>();
        vehicle.input.forward = 1.0f;
        for (int frame = 0; frame < 5 * 60; ++frame)
        {
            test.app.RunFrame(tick);
            for (const Entity& wheel : car.wheels)
            {
                run.slip = frame < 4 * 60 ? 0.0f : std::max(run.slip, std::abs(StateOf(wheel).longitudinalSlip));
            }
        }
        run.speed = StateOf(car).forwardSpeed;
        const Math::Vec3 from = PositionOf(car.chassis);
        vehicle.input.forward = 0.0f;
        vehicle.input.brake = 1.0f;
        for (int frame = 0; frame < 20 * 60 && StateOf(car).forwardSpeed > 0.05f; ++frame)
        {
            test.app.RunFrame(tick);
        }
        // Straight ahead, along -Z.
        run.braking = from.z - PositionOf(car.chassis).z;
        return run;
    }
} // namespace

TEST_CASE("Each wheel's probe finds only what the chassis's CollisionFilter collides with, both ways round",
          "[physics][vehicles][ground]")
{
    INFO("a floor and a chassis that collide find each other");
    REQUIRE(WheelsOnLayeredFloor(~0u, CollisionFilter{}) == 4);
    INFO("a chassis whose mask leaves out the floor's layer finds no floor");
    REQUIRE(WheelsOnLayeredFloor(~0u, CollisionFilter{.layers = 1u, .mask = 1u}) == 0);
    INFO("a floor whose mask leaves out the chassis's layer is not found either: the rule is two-sided");
    REQUIRE(WheelsOnLayeredFloor(1u << 1, CollisionFilter{}) == 0);
    INFO("with the chassis in a layer the floor's mask includes, it is found again");
    REQUIRE(WheelsOnLayeredFloor(1u << 1, CollisionFilter{.layers = 1u << 1}) == 4);
}

TEST_CASE("Probes find the floor through a trigger lying on it, and never the vehicle's own chassis",
          "[physics][vehicles][ground]")
{
    PhysicsApp test;
    Entity floor = AddFloor(test.Entities());
    // A trigger 0.4 m deep on the floor, under the whole car: every probe passes through it.
    Entity pad = Place(test.Entities(), Math::Vec3(0.0f, 0.2f, 0.0f));
    pad.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(5.0f, 0.2f, 5.0f)}});
    pad.AddComponent<Trigger>();
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);

    for (const Entity& wheel : car.wheels)
    {
        INFO("each wheel stands on the floor at y = 0, not on the trigger or inside its own chassis");
        REQUIRE(StateOf(wheel).onGround);
        REQUIRE(StateOf(wheel).groundBody == floor);
        REQUIRE(StateOf(wheel).contactPoint.y == Approx(0.0f).margin(0.02));
    }
    INFO("no wheel names its own chassis as ground, though every probe starts inside it");
    REQUIRE_FALSE(AnyWheelOn(car, car.chassis));
}

TEST_CASE("A wheel above a character never stands on it: probes skip a character's body", "[physics][vehicles][ground]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    // The character stands under the front left wheel, its head 1.6 m up; the car is dropped from above it, so that
    // wheel's probe would land on the capsule if it saw it.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-wheelX, 0.0f, -wheelZ));
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 2.6f, 0.0f));
    for (int frame = 0; frame < 3 * 60; ++frame)
    {
        test.app.RunFrame(tick);
        INFO("tick " << frame << ": no wheel names the character as its ground");
        REQUIRE_FALSE(AnyWheelOn(car, character));
    }
}

TEST_CASE("A car driven at a character is stopped by its chassis, and no wheel climbs the capsule",
          "[physics][vehicles][ground]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    const float standing = -8.0f;
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, standing));
    // Game code standing still: no walk, gravity added.
    PhysicsTest::Drive(test.app, character);
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.2f; });
    float fastest = 0.0f;
    for (int frame = 0; frame < 8 * 60; ++frame)
    {
        test.app.RunFrame(tick);
        fastest = std::max(fastest, StateOf(car).forwardSpeed);
        INFO("tick " << frame << ": no wheel names the character as its ground, and every wheel stays down");
        REQUIRE_FALSE(AnyWheelOn(car, character));
        REQUIRE(StateOf(car).wheelsOnGround == 4);
    }

    INFO("the car got going (" << fastest << " m/s) and is now held still at the character (car at "
                               << PositionOf(car.chassis).x << ", " << PositionOf(car.chassis).z << "; character at "
                               << PositionOf(character).x << ", " << PositionOf(character).z << ")");
    REQUIRE(fastest > 2.0f);
    REQUIRE(std::abs(StateOf(car).forwardSpeed) < 0.2f);
    const float nose = PositionOf(car.chassis).z - 2.0f;
    INFO("its nose (" << nose << ") stops at the capsule's near side (" << standing + 0.3f << ")");
    REQUIRE(nose == Approx(standing + 0.3f).margin(0.15f));
    INFO("the character, an immovable wall to bodies, has not been pushed");
    REQUIRE(PositionOf(character).z == Approx(standing).margin(0.01f));
}

TEST_CASE("A character walking into a parked car pushes it with its pushForce, which barely moves it",
          "[physics][vehicles][ground]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    // Behind the car, walking into its tail along -Z, the way its wheels roll.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 2.6f));
    auto input = PhysicsTest::Drive(test.app, character);
    test.Run(1.0);
    const Math::Vec3 parked = PositionOf(car.chassis);
    input->walk = Math::Vec3(0.0f, 0.0f, -2.0f);
    test.Run(3.0);

    const float moved = parked.z - PositionOf(car.chassis).z;
    INFO("the car is pushed ahead of the character, only a little (" << moved << " m in 3 s)");
    REQUIRE(moved >= 0.0f);
    REQUIRE(moved < 0.5f);
    INFO("the character is held at the car's tail");
    REQUIRE(PositionOf(character).z > PositionOf(car.chassis).z + 2.0f);
}

TEST_CASE("A character standing on a chassis rides it and presses on it with its weight", "[physics][vehicles][ground]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    const float unloaded = StateOf(car.wheels[0]).suspensionForce + StateOf(car.wheels[1]).suspensionForce +
                           StateOf(car.wheels[2]).suspensionForce + StateOf(car.wheels[3]).suspensionForce;
    Entity character = AddCharacter(test.Entities(), PositionOf(car.chassis) + Math::Vec3(0.0f, 0.3f, 0.0f));
    PhysicsTest::Drive(test.app, character);
    test.Run(1.0);
    float loaded = 0.0f;
    for (const Entity& wheel : car.wheels)
    {
        loaded += StateOf(wheel).suspensionForce;
    }
    INFO("the character stands on the chassis, and the suspensions carry its weight too (" << unloaded << " N, then "
                                                                                           << loaded << " N)");
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == car.chassis);
    REQUIRE(loaded - unloaded == Approx(CharacterBody{}.mass * 9.81f).epsilon(0.3));

    const Math::Vec3 onTop = PositionOf(character) - PositionOf(car.chassis);
    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.2f; });
    test.Run(2.0);
    const Math::Vec3 after = PositionOf(character) - PositionOf(car.chassis);
    INFO("driven 2 s, the car carries the character where it stood on it (" << onTop.z << ", then " << after.z << ")");
    REQUIRE(StateOf(car).forwardSpeed > 1.0f);
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == car.chassis);
    REQUIRE(after.z == Approx(onTop.z).margin(0.3f));
}

TEST_CASE("Grip follows the ground's friction as the spike measured: ice spins the wheels, grass is slower than tarmac",
          "[physics][vehicles][ground]")
{
    const GripRun ice = DriveOn(0.05f);
    const GripRun grass = DriveOn(0.3f);
    const GripRun tarmac = DriveOn(0.5f);
    const VehicleWheel wheel;
    INFO("after 5 s at full throttle: ice "
         << ice.speed / kmh << " km/h (slip " << ice.slip << "), grass " << grass.speed / kmh << " km/h, tarmac "
         << tarmac.speed / kmh << " km/h; braking ice " << ice.braking << " m, grass " << grass.braking << " m, tarmac "
         << tarmac.braking << " m");
    INFO("on ice the wheels spin, slipping past slideSlip (the spike: 1.1), and the car is far slower (34 km/h)");
    REQUIRE(ice.slip > wheel.longitudinal.slideSlip);
    REQUIRE(ice.speed / kmh == Approx(34.0f).epsilon(0.15));
    INFO("grass is slower than tarmac (the spike: 72 and 79 km/h)");
    REQUIRE(grass.speed < tarmac.speed);
    REQUIRE(grass.speed / kmh == Approx(72.0f).epsilon(0.1));
    REQUIRE(tarmac.speed / kmh == Approx(79.0f).epsilon(0.1));
    INFO("braking distances as the spike's: 21.7 m on ice (about 0.22 g), 36 m on grass, 38 m on tarmac");
    REQUIRE(ice.braking == Approx(21.7f).epsilon(0.15));
    REQUIRE(grass.braking == Approx(36.0f).epsilon(0.15));
    REQUIRE(tarmac.braking == Approx(38.0f).epsilon(0.15));
}

TEST_CASE("A car on a dynamic slab pushes it back as it pulls away, standing on it and loading it",
          "[physics][vehicles][ground]")
{
    PhysicsApp test;
    // A frictionless floor, so only the wheels move the slab.
    AddFloor(test.Entities(), 0.0f);
    Entity slab = AddBody(test.Entities(), Math::Vec3(0.0f, 0.1f, 0.0f), BoxShape{Math::Vec3(3.0f, 0.1f, 6.0f)});
    slab.GetComponent<RigidBody>().mass = 1000.0f;
    slab.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = 0.5f});
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.2f, 0.0f));
    test.Run(1.0);
    for (const Entity& wheel : car.wheels)
    {
        INFO("every wheel stands on the slab and carries part of the car");
        REQUIRE(StateOf(wheel).groundBody == slab);
        REQUIRE(StateOf(wheel).suspensionForce > 0.0f);
    }
    const float start = PositionOf(slab).z;

    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.2f; });
    test.Run(1.5);
    INFO("the car moves forward (-Z), and the slab is pushed back (+Z) under it");
    REQUIRE(StateOf(car).forwardSpeed > 0.5f);
    REQUIRE(PositionOf(slab).z - start > 0.1f);
    INFO("the car is still on the slab, pushing it");
    REQUIRE(AnyWheelOn(car, slab));
}

TEST_CASE("A parked car rides a kinematic platform through its tyres' grip, its wheels reading the platform's "
          "velocity",
          "[physics][vehicles][ground]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    // A platform 10 m square and 0.2 m thick, its top 0.5 m up.
    Entity platform = AddBody(test.Entities(), Math::Vec3(0.0f, 0.4f, 0.0f), BoxShape{Math::Vec3(5.0f, 0.1f, 5.0f)},
                              MotionType::Kinematic);
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    car.chassis.GetComponent<Vehicle>().input.brake = 1.0f;
    test.Run(1.0);
    REQUIRE(StateOf(car).wheelsOnGround == 4);
    const Math::Vec3 parked = PositionOf(car.chassis) - PositionOf(platform);

    // Sideways to the car at 2 m/s, so only the tyres' grip carries it.
    const Math::Vec3 velocity(2.0f, 0.0f, 0.0f);
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[platform, velocity](App&) {
                                      platform.GetComponent<Transform>().pos += velocity * dt;
                                  }});
    // The tyres slip a little while the car takes up the platform's speed, then hold.
    test.Run(1.0);
    const Math::Vec3 carried = PositionOf(car.chassis) - PositionOf(platform);
    test.Run(2.0);

    const Math::Vec3 after = PositionOf(car.chassis) - PositionOf(platform);
    INFO("the platform moved 6 m and the car with it (" << parked.x << ", " << carried.x << " then " << after.x
                                                        << " across it)");
    REQUIRE(PositionOf(platform).x == Approx(6.0f).margin(0.1f));
    REQUIRE(carried.x == Approx(parked.x).margin(0.5f));
    REQUIRE(after.x == Approx(carried.x).margin(0.05f));
    REQUIRE(after.z == Approx(carried.z).margin(0.05f));
    for (const Entity& wheel : car.wheels)
    {
        INFO("each wheel stands on the platform, moving with it");
        REQUIRE(StateOf(wheel).groundBody == platform);
        REQUIRE(StateOf(wheel).groundVelocity.x == Approx(velocity.x).margin(0.05f));
    }
}

TEST_CASE("Queries find only a vehicle's chassis, naming its entity, and ignoreBody skips it",
          "[physics][vehicles][ground]")
{
    PhysicsApp test;
    Entity floor = AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    const PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    const Math::Vec3 down(0.0f, -1.0f, 0.0f);
    const Math::Vec3 above = PositionOf(car.chassis) + Math::Vec3(0.0f, 5.0f, 0.0f);

    const std::optional<RaycastHit> hit = world->Raycast(above, down, 20.0f);
    INFO("a ray down onto the car hits the chassis, naming its entity");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == car.chassis);
    const std::optional<RaycastHit> skipped =
        world->Raycast(above, down, 20.0f, QueryFilter{.ignoreBody = car.chassis});
    INFO("ignoring the chassis entity, it goes through the car to the floor");
    REQUIRE(skipped.has_value());
    REQUIRE(skipped->body == floor);

    // Along the front axle, below the chassis: through both front wheels and nothing else.
    const Math::Vec3 axle = PositionOf(car.wheels[0]);
    const std::optional<RaycastHit> wheels =
        world->Raycast(axle - Math::Vec3(5.0f, 0.0f, 0.0f), Math::Vec3(1.0f, 0.0f, 0.0f), 10.0f);
    INFO("a ray through the wheels finds nothing: a wheel is not a body");
    REQUIRE_FALSE(wheels.has_value());
}

TEST_CASE("A ball dropped onto a wheel falls through it", "[physics][vehicles][ground]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    // Over the outer half of the front left wheel, beside the chassis.
    const Math::Vec3 wheel = PositionOf(car.wheels[0]);
    const float radius = 0.02f;
    Entity ball = AddBody(test.Entities(), Math::Vec3(wheel.x - 0.03f, 3.0f, wheel.z), SphereShape{radius});
    test.Run(2.0);
    INFO("it falls past the wheel's middle (" << wheel.y << ") to the floor (" << PositionOf(ball).y << ")");
    REQUIRE(PositionOf(ball).y < 0.1f);
}
