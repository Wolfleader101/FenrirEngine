// Getting in and out of a vehicle, game code's recipe in the Vehicle docs: a disabled character parented under the
// chassis rides it unseen by the chassis, its wheels' probes and queries, and is cleared with Depenetrate when it gets
// out.

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    const CollisionShape capsule = CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f};

    // A seat over the front-left wheel, chassis-local: the seated capsule reaches below the chassis through that
    // wheel's probe, so a probe that saw it would stand on it.
    const Math::Vec3 seat(-wheelX, 0.3f, -wheelZ);

    struct Seated
    {
        Entity floor;
        Car car;
        Entity character;
    };

    // A car settled on the floor, and a character beside it, walked by the character tests' game code, that has got
    // in: disabled, then parented under the chassis at the seat.
    Seated GetIn(PhysicsApp& test)
    {
        Seated seated{.floor = AddFloor(test.Entities()),
                      .car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f)),
                      .character = AddCharacter(test.Entities(), Math::Vec3(-3.0f, 0.0f, 0.0f))};
        PhysicsTest::Drive(test.app, seated.character);
        test.Run(1.0);
        seated.character.GetComponent<CharacterBody>().enabled = false;
        seated.car.chassis.AddChild(seated.character);
        seated.character.GetComponent<Transform>() = Transform(seat, identity, Math::Vec3(1.0f));
        return seated;
    }

    // What the capsule overlaps at `pose`, skipping `ignore`.
    std::size_t PenetrationsAt(const PhysicsWorld& world, const Transform& pose, const Entity& ignore = Entity())
    {
        std::array<PenetrationHit, 8> hits;
        return world.Penetrations(capsule, pose, hits, QueryFilter{.ignoreBody = ignore});
    }
} // namespace

TEST_CASE("A disabled character seated under a chassis rides it, unseen by the chassis, the probes and queries",
          "[physics][vehicles][drivers]")
{
    PhysicsApp test;
    // An empty car to compare the chassis's body with, well away.
    Car empty = AddCar(test.Entities(), Math::Vec3(20.0f, 1.0f, 0.0f));
    Seated seated = GetIn(test);
    const Car& car = seated.car;
    const PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());

    Drive(test.app, car.chassis, [](int, Vehicle& vehicle) { vehicle.input.forward = 0.3f; });
    for (int frame = 0; frame < 3 * 60; ++frame)
    {
        test.app.RunFrame(tick);
        INFO("tick " << frame << ": every wheel stands on the floor, the seated capsule unseen by its probe");
        for (const Entity& wheel : car.wheels)
        {
            REQUIRE(StateOf(wheel).onGround);
            REQUIRE(StateOf(wheel).groundBody == seated.floor);
        }
    }
    INFO("the car drove off with its driver");
    REQUIRE(StateOf(car).forwardSpeed > 2.0f);

    const Transform chassisAt = GetWorldTransform(car.chassis);
    const Math::Vec3 seatAt = chassisAt.pos + chassisAt.rot * seat;
    const Transform sitting = GetWorldTransform(seated.character);
    INFO("the character is at its seat on the chassis's pose (" << sitting.pos.x << ", " << sitting.pos.y << ", "
                                                                << sitting.pos.z << ")");
    REQUIRE(sitting.pos.x == Approx(seatAt.x).margin(1e-4));
    REQUIRE(sitting.pos.y == Approx(seatAt.y).margin(1e-4));
    REQUIRE(sitting.pos.z == Approx(seatAt.z).margin(1e-4));

    const std::optional<Math::AABB> carrying = world.Bounds(car.chassis);
    const std::optional<Math::AABB> alone = world.Bounds(empty.chassis);
    REQUIRE(carrying.has_value());
    REQUIRE(alone.has_value());
    INFO("the chassis's body is its box alone, as tall as an empty car's, not grown by the capsule hanging below it ("
         << carrying->max.y - carrying->min.y << " m, empty " << alone->max.y - alone->min.y << " m)");
    REQUIRE(carrying->max.y - carrying->min.y == Approx(alone->max.y - alone->min.y).margin(0.05f));

    std::array<OverlapHit, 8> overlaps;
    const std::size_t found = world.Overlap(capsule, sitting, overlaps);
    INFO("an overlap at the seat finds the chassis (" << found << " hits), never the character");
    REQUIRE(found > 0);
    REQUIRE(std::ranges::none_of(std::span(overlaps).first(found),
                                 [&seated](const OverlapHit& hit) { return hit.body == seated.character; }));
    const std::optional<RaycastHit> down =
        world.Raycast(sitting.pos + Math::Vec3(0.0f, 3.0f, 0.0f), Math::Vec3(0.0f, -1.0f, 0.0f), 10.0f,
                      QueryFilter{.ignoreBody = car.chassis});
    INFO("a ray down through the seat, skipping the chassis, reaches the floor");
    REQUIRE(down.has_value());
    REQUIRE(down->body == seated.floor);
}

TEST_CASE("A character getting out beside the chassis is cleared by Depenetrate and, enabled, overlaps nothing",
          "[physics][vehicles][drivers]")
{
    PhysicsApp test;
    Seated seated = GetIn(test);
    test.Run(1.0);
    const PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());

    // Unparented, its world pose written beside the driver's door, overlapping the chassis's side a little as a
    // careless door would.
    const Transform car = GetWorldTransform(seated.car.chassis);
    seated.car.chassis.RemoveChild(seated.character);
    const Math::Vec3 door = car.pos + car.rot * Math::Vec3(-1.0f, 0.0f, -0.3f);
    Transform beside(Math::Vec3(door.x, footToCentre + 0.05f, door.z), identity, Math::Vec3(1.0f));
    INFO("placed beside the door, the capsule is inside the chassis");
    REQUIRE(PenetrationsAt(world, beside) > 0);
    beside.pos += world.Depenetrate(capsule, beside);
    INFO("cleared by Depenetrate, it overlaps nothing");
    REQUIRE(PenetrationsAt(world, beside) == 0);

    seated.character.GetComponent<Transform>() = beside;
    // The test's game code went on adding gravity while it sat: it gets out still.
    CharacterBody& body = seated.character.GetComponent<CharacterBody>();
    body.velocity = Math::Vec3(0.0f);
    body.enabled = true;
    test.Run(1.0);

    INFO("enabled again, it stands on the floor beside the car where it was put, still overlapping nothing");
    REQUIRE(body.state.ground == GroundState::OnGround);
    REQUIRE(body.state.groundBody == seated.floor);
    REQUIRE(PenetrationsAt(world, GetWorldTransform(seated.character), seated.character) == 0);
    REQUIRE(Math::Magnitude(PositionOf(seated.character) - beside.pos) < 0.1f);
}
