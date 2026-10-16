// Rigid bodies through the frame seam: what game code sees in its components and world transforms as physics steps.

#include <cstring>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

TEST_CASE("A dynamic box falls and comes to rest on a static ground", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));

    test.Run(3.0);

    const Math::Vec3 rest = PositionOf(box);
    INFO("a unit box rests with its centre half a unit above the ground's top (y = 0), it is at " << rest.y);
    REQUIRE(rest.y == Approx(0.5f).margin(restSlop));
    INFO("it fell straight down, so x and z are unchanged");
    REQUIRE(rest.x == Approx(0.0f).margin(1e-4));
    REQUIRE(rest.z == Approx(0.0f).margin(1e-4));
}

TEST_CASE("A dynamic sphere falls and comes to rest on a static ground", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity ball = AddBody(test.Entities(), Math::Vec3(2.0f, 4.0f, -1.0f), SphereShape{0.25f});

    test.Run(3.0);

    const Math::Vec3 rest = PositionOf(ball);
    INFO("a sphere of radius 0.25 rests with its centre 0.25 above the ground, it is at " << rest.y);
    REQUIRE(rest.y == Approx(0.25f).margin(restSlop));
}

TEST_CASE("A falling body's velocity is written back to its RigidBody", "[physics][bodies]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));

    test.Run(0.5);

    const float fallSpeed = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("after half a second of free fall at 9.81 the body moves down at about 4.9 (damping slows it a little), it "
         "moves at "
         << fallSpeed);
    REQUIRE(fallSpeed == Approx(-4.905f).margin(0.1f));
}

TEST_CASE("A collider with no RigidBody is static", "[physics][bodies]")
{
    PhysicsApp test;
    Entity floating = Place(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));
    floating.AddComponent<Collider>();

    test.Run(1.0);

    INFO("a collider without a RigidBody does not fall");
    REQUIRE(PositionOf(floating).y == 5.0f);
}

TEST_CASE("A kinematic body moved in Tick pushes a dynamic body, and stops when it is no longer moved",
          "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity paddle = AddBody(test.Entities(), Math::Vec3(-2.0f, 0.5f, 0.0f), BoxShape{Math::Vec3(0.25f, 0.5f, 1.0f)},
                            MotionType::Kinematic);

    // Game code sweeps the paddle 2 m to the right over one second, then leaves it where it is.
    int ticks = 0;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                      if (ticks++ < 60)
                                      {
                                          paddle.GetComponent<Transform>().pos.x += 2.0f / 60.0f;
                                      }
                                  }});

    test.Run(1.0);
    const float pushedTo = PositionOf(box).x;
    INFO("the paddle's right face swept from x = -1.75 to 0.25, so the box (half width 0.5) was pushed to at least "
         "x = 0.75 less slop; it is at "
         << pushedTo);
    REQUIRE(pushedTo > 0.7f);

    test.Run(1.0);
    const float settled = PositionOf(box).x;
    test.Run(1.0);
    INFO("once the paddle stops being moved it stops, so the box it pushed stays put (" << settled << " then "
                                                                                        << PositionOf(box).x << ")");
    REQUIRE(PositionOf(box).x == Approx(settled).margin(1e-3));
    INFO("the kinematic paddle is where game code left it; nothing pushed it");
    REQUIRE(PositionOf(paddle).x == Approx(0.0f).margin(1e-4));
}

TEST_CASE("Writing a dynamic body's Transform teleports it", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(0.5);

    box.GetComponent<Transform>().pos = Math::Vec3(3.0f, 4.0f, 2.0f);
    test.Run(tick);
    const Math::Vec3 moved = PositionOf(box);
    INFO("one tick after the write the body is at the written place, fallen a little: " << moved.x << ", " << moved.y
                                                                                        << ", " << moved.z);
    REQUIRE(moved.x == Approx(3.0f).margin(1e-3));
    REQUIRE(moved.y == Approx(4.0f).margin(0.01));
    REQUIRE(moved.z == Approx(2.0f).margin(1e-3));

    test.Run(2.0);
    INFO("it then falls and rests on the ground at its new place");
    REQUIRE(PositionOf(box).x == Approx(3.0f).margin(1e-3));
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("Writing a dynamic body's velocity sets it", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(0.5);

    box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f, 10.0f, 0.0f);
    test.Run(tick);

    const float upward = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("launched up at 10, one tick of gravity later it rises at about 10 - 9.81 / 60 = 9.84, it rises at "
         << upward);
    REQUIRE(upward == Approx(9.84f).margin(0.02));
    INFO("and it has left the ground");
    REQUIRE(PositionOf(box).y > 0.6f);
}

TEST_CASE("Switching a body's motion type takes effect on the next tick", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f), BoxShape{}, MotionType::Static);
    test.Run(0.5);
    INFO("a static RigidBody does not fall");
    REQUIRE(PositionOf(box).y == 5.0f);

    box.GetComponent<RigidBody>().motionType = MotionType::Dynamic;
    test.Run(0.5);
    const float falling = PositionOf(box).y;
    INFO("made dynamic, it falls (it is at " << falling << ")");
    REQUIRE(falling < 4.0f);

    box.GetComponent<RigidBody>().motionType = MotionType::Kinematic;
    test.Run(0.5);
    INFO("made kinematic and not moved by game code, it stays where it was");
    REQUIRE(PositionOf(box).y == Approx(falling).margin(1e-6));

    box.GetComponent<RigidBody>().motionType = MotionType::Dynamic;
    test.Run(3.0);
    INFO("made dynamic again, it falls to the ground");
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("Assigning a different kind of shape to a collider takes effect", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity body = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(0.5);

    body.GetComponent<Collider>().shape = SphereShape{1.0f};
    test.Run(2.0);

    INFO("a sphere of radius 1 rests with its centre 1 above the ground, it is at " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(1.0f).margin(restSlop));
}

TEST_CASE("Editing a shape's dimensions in place takes effect", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(0.5);

    box.GetComponent<Collider>().shape.As<BoxShape>().halfExtents.y = 2.0f;
    test.Run(2.0);

    INFO("a box of half height 2 rests with its centre 2 above the ground, it is at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(2.0f).margin(restSlop));
}

TEST_CASE("Changing an entity's scale scales its collider", "[physics][bodies]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(0.5);

    box.GetComponent<Transform>().scale = Math::Vec3(3.0f);
    test.Run(2.0);

    INFO("a unit box scaled by 3 rests with its centre 1.5 above the ground, it is at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(1.5f).margin(restSlop));
}

namespace
{
    struct Outcome
    {
        Transform box;
        Transform ball;
        RigidBody boxBody;
    };

    // Boxes and a ball tumbling onto each other and the ground, with a teleport and a velocity write on the way.
    Outcome Tumble()
    {
        PhysicsApp test;
        AddGround(test.Entities());
        Entity box = AddBody(test.Entities(), Math::Vec3(0.1f, 2.0f, 0.0f));
        box.GetComponent<Transform>().rot = Math::AngleAxis(0.4f, Math::Normalized(Math::Vec3(1.0f, 1.0f, 0.0f)));
        AddBody(test.Entities(), Math::Vec3(-0.2f, 3.5f, 0.1f));
        Entity ball = AddBody(test.Entities(), Math::Vec3(0.3f, 5.0f, -0.2f), SphereShape{0.4f});
        test.Run(1.0);
        ball.GetComponent<RigidBody>().linearVelocity = Math::Vec3(1.0f, 2.0f, 0.0f);
        test.Run(0.5);
        box.GetComponent<Transform>().pos.y += 1.0f;
        test.Run(1.5);
        return Outcome{box.GetComponent<Transform>(), ball.GetComponent<Transform>(), box.GetComponent<RigidBody>()};
    }

    bool BitIdentical(const Math::Vec3& a, const Math::Vec3& b)
    {
        return std::memcmp(&a, &b, sizeof(Math::Vec3)) == 0;
    }

    bool BitIdentical(const Math::Quat& a, const Math::Quat& b)
    {
        return std::memcmp(&a, &b, sizeof(Math::Quat)) == 0;
    }
} // namespace

TEST_CASE("The same inputs give bit-identical results", "[physics][bodies]")
{
    const Outcome first = Tumble();
    const Outcome second = Tumble();

    INFO("two runs of the same scene leave the bodies at the same poses and velocities, bit for bit");
    REQUIRE(BitIdentical(first.box.pos, second.box.pos));
    REQUIRE(BitIdentical(first.box.rot, second.box.rot));
    REQUIRE(BitIdentical(first.ball.pos, second.ball.pos));
    REQUIRE(BitIdentical(first.ball.rot, second.ball.rot));
    REQUIRE(BitIdentical(first.boxBody.linearVelocity, second.boxBody.linearVelocity));
    REQUIRE(BitIdentical(first.boxBody.angularVelocity, second.boxBody.angularVelocity));
}
