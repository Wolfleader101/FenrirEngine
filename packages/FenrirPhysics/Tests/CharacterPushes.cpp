// Characters among other bodies: a character pushes the dynamic bodies it walks into with a capped force, presses its
// weight on its ground, is never pushed by a dynamic body and is shoved by a kinematic one at that body's speed.

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

TEST_CASE("A ball thrown at a standing character bounces off without moving it", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    test.Run(0.25);
    const Math::Vec3 standing = PositionOf(character);

    Entity ball = Place(test.Entities(), Math::Vec3(-3.0f, footToCentre, 0.0f));
    ball.AddComponent<RigidBody>(
        RigidBody{.mass = 1.0f, .gravityScale = 0.0f, .linearVelocity = Math::Vec3(10.0f, 0.0f, 0.0f)});
    ball.AddComponent<Collider>(Collider{SphereShape{.radius = 0.15f}});
    ball.AddComponent<PhysicsMaterial>(PhysicsMaterial{.restitution = 0.8f});

    for (int t = 0; t < 60; ++t)
    {
        test.Run(tick);
        INFO("tick " << t << ": character at " << PositionOf(character).x << ", ball at " << PositionOf(ball).x);
        REQUIRE(std::abs(PositionOf(character).x - standing.x) < 1.0e-4f);
        REQUIRE(std::abs(PositionOf(character).z - standing.z) < 1.0e-4f);
    }
    INFO("the ball came back off the character");
    REQUIRE(ball.GetComponent<RigidBody>().linearVelocity.x < -1.0f);
}

namespace
{
    // A 4 m plank, 10 kg, hinged to the world at its middle 0.6 m up, level: it tips until an end meets the ground.
    Entity AddSeesaw(EntityList& entities)
    {
        Entity plank = AddBody(entities, Math::Vec3(0.0f, 0.6f, 0.0f), BoxShape{Math::Vec3(2.0f, 0.05f, 0.5f)});
        plank.GetComponent<RigidBody>().mass = 10.0f;
        // A hinge turns about its frame's X axis: here the world's Z.
        const Math::Quat alongZ = Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 1.0f, 0.0f));
        const JointFrames frames = FrameFromWorld(plank, Entity(), JointFrame{Math::Vec3(0.0f, 0.6f, 0.0f), alongZ});
        plank.AddComponent<Joint>(Joint{
            .bodyA = plank, .bodyB = Entity(), .frameA = frames.frameA, .frameB = frames.frameB, .kind = HingeJoint{}});
        return plank;
    }

    // Radians the plank has turned from level, positive with its +X end down.
    float TiltOf(const Entity& plank)
    {
        const Math::Vec3 along = GetWorldTransform(plank).rot * Math::Vec3(1.0f, 0.0f, 0.0f);
        return -std::asin(along.y);
    }
} // namespace

TEST_CASE("A character's weight tips a seesaw it stands on one end of, and with mass 0 it does not",
          "[physics][characters]")
{
    const float mass = GENERATE(70.0f, 0.0f);
    PhysicsApp test;
    AddGround(test.Entities());
    Entity plank = AddSeesaw(test.Entities());
    // Its feet just above the plank's top, 1.5 m out along +X.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(1.5f, 0.66f, 0.0f));
    character.GetComponent<CharacterBody>().mass = mass;
    Drive(test.app, character);

    test.Run(1.0);
    INFO("mass " << mass << ": tilt " << TiltOf(plank) << " rad, character's feet at " << FeetOf(character));
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == plank);
    if (mass > 0.0f)
    {
        // Its end comes down 0.55 m, onto the ground, over 2 m: about 0.28 rad.
        REQUIRE(TiltOf(plank) > 0.2f);
    }
    else
    {
        REQUIRE(std::abs(TiltOf(plank)) < 0.01f);
    }
}

namespace
{
    // A box of `mass` kg and `halfExtent` m, its bottom on the ground, `ahead` m along +X, whose own friction and the
    // ground's are both `friction`.
    Entity AddCrate(EntityList& entities, Entity ground, float mass, float halfExtent, float ahead, float friction)
    {
        Entity crate = AddBody(entities, Math::Vec3(ahead, halfExtent, 0.0f), BoxShape{Math::Vec3(halfExtent)});
        crate.GetComponent<RigidBody>().mass = mass;
        crate.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = friction});
        ground.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = friction});
        return crate;
    }
} // namespace

TEST_CASE("A character walking into a 1 kg box pushes it along at its own pace", "[physics][characters]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity box = AddCrate(test.Entities(), ground, 1.0f, 0.5f, 1.0f, 0.5f);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character)->walk = Math::Vec3(2.0f, 0.0f, 0.0f);

    test.Run(1.5);
    const float from = PositionOf(box).x;
    test.Run(1.0);
    INFO("the box went " << PositionOf(box).x - from << " m in a second, the character to " << PositionOf(character).x);
    REQUIRE(PositionOf(box).x - from == Approx(2.0f).margin(0.3f));
    REQUIRE(PositionOf(box).x > PositionOf(character).x);
}

TEST_CASE("A character pushes a 20 kg crate on friction 0.2 at about 3 m/s^2, its force capped by pushForce",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity crate = AddCrate(test.Entities(), ground, 20.0f, 0.5f, 1.0f, 0.2f);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character)->walk = Math::Vec3(4.0f, 0.0f, 0.0f);

    // It reaches the crate in about a twentieth of a second and pushes it from then on, never catching up with it:
    // from the start, a move that hits the crate more than once still pushes with pushForce at most.
    test.Run(0.1);
    const float before = crate.GetComponent<RigidBody>().linearVelocity.x;
    test.Run(0.5);
    const float after = crate.GetComponent<RigidBody>().linearVelocity.x;
    // (100 N - 0.2 × 20 kg × 9.81 m/s²) / 20 kg = 3.04 m/s².
    INFO("crate speed " << before << " then " << after << " m/s");
    REQUIRE(before > 0.0f);
    REQUIRE((after - before) / 0.5f == Approx(3.04f).margin(0.3f));
}

TEST_CASE("A character does not move a 100 kg crate on friction 0.5, nor a 1 kg box with pushForce 0",
          "[physics][characters]")
{
    const bool heavy = GENERATE(true, false);
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity crate = heavy ? AddCrate(test.Entities(), ground, 100.0f, 0.5f, 1.0f, 0.5f)
                         : AddCrate(test.Entities(), ground, 1.0f, 0.5f, 1.0f, 0.5f);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    if (!heavy)
    {
        character.GetComponent<CharacterBody>().pushForce = 0.0f;
    }
    Drive(test.app, character)->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    test.Run(0.25);
    const Math::Vec3 resting = PositionOf(crate);

    test.Run(1.0);
    INFO((heavy ? "100 kg" : "pushForce 0") << ": the crate moved " << PositionOf(crate).x - resting.x
                                            << " m, the character stopped at " << PositionOf(character).x);
    REQUIRE(std::abs(PositionOf(crate).x - resting.x) < 1.0e-3f);
    REQUIRE(character.GetComponent<CharacterBody>().state.onWall);
}

TEST_CASE("A character landing on a floating box with mass 0 does not push it down", "[physics][characters]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.5f, 1.0f)});
    box.GetComponent<RigidBody>().gravityScale = 0.0f;
    // Falling diagonally onto its top: the push would be along the top's normal, straight down.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-0.3f, 2.0f, 0.0f));
    character.GetComponent<CharacterBody>().mass = 0.0f;
    Drive(test.app, character)->walk = Math::Vec3(0.5f, 0.0f, 0.0f);

    bool landed = false;
    for (int t = 0; t < 60; ++t)
    {
        test.Run(tick);
        landed = landed || character.GetComponent<CharacterBody>().state.groundBody == box;
        INFO("tick " << t << ": box velocity.y " << box.GetComponent<RigidBody>().linearVelocity.y);
        REQUIRE(box.GetComponent<RigidBody>().linearVelocity.y > -1.0e-3f);
    }
    INFO("the character landed on the box");
    REQUIRE(landed);
}

TEST_CASE("A kinematic paddle shoves a standing character at its own speed, shown in the written-back velocity",
          "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity paddle = AddBody(test.Entities(), Math::Vec3(-1.5f, 0.5f, 0.0f), BoxShape{Math::Vec3(0.2f, 0.5f, 1.0f)},
                            MotionType::Kinematic);
    constexpr float paddleSpeed = 1.5f;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[paddle](App& running) {
                                      paddle.GetComponent<Transform>().pos.x +=
                                          paddleSpeed * static_cast<float>(running.GetTime().tickRate);
                                  }});
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    // Game code asks for no horizontal speed every tick: the shove is the paddle's.
    Drive(test.app, character);

    // It reaches the character after (1.5 - 0.2 - 0.3) / 1.5 s, two thirds of a second.
    test.Run(1.0);
    for (int t = 0; t < 30; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << t << ": moved at " << body.state.movedVelocity.x << ", velocity " << body.velocity.x);
        REQUIRE(body.state.movedVelocity.x == Approx(paddleSpeed).margin(0.02f));
        REQUIRE(body.velocity.x == Approx(paddleSpeed).margin(0.02f));
        REQUIRE(body.state.ground == GroundState::OnGround);
    }
}
