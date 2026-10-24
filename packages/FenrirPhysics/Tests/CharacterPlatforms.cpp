// A character rides the walkable ground it stands on (lifts, shuttles, rafts, turntables) with no game code, and keeps
// the ground's velocity as carried velocity when it leaves it, until it lands.

#include <cmath>
#include <functional>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float dt = static_cast<float>(tick);

    // A kinematic platform 2 m square and 0.2 m thick, its top face at `top`.
    Entity AddPlatform(EntityList& entities, const Math::Vec3& top)
    {
        return AddBody(entities, top - Math::Vec3(0.0f, 0.1f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.1f, 1.0f)},
                       MotionType::Kinematic);
    }

    // Game code moving a kinematic platform in Tick: by `velocity(t)` over its t-th tick, counted from 0.
    void Carry(App& app, const Entity& platform, std::function<Math::Vec3(int)> velocity)
    {
        auto ticks = std::make_shared<int>(0);
        app.AddSequentialSystems(SchedulePriority::Tick, {[ticks, platform, velocity](App&) {
                                     platform.GetComponent<Transform>().pos += velocity((*ticks)++) * dt;
                                 }});
    }

    float TopOf(const Entity& platform)
    {
        return PositionOf(platform).y + 0.1f;
    }

    // Lets a character land on what is under it and settle.
    void Settle(PhysicsApp& test)
    {
        test.Run(0.25);
    }
} // namespace

TEST_CASE("A character rides a lift up and down, standing on it and naming it, with no game code",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity lift = AddPlatform(test.Entities(), Math::Vec3(0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.3f, 0.0f, -0.2f));
    Drive(test.app, character);
    Settle(test);

    // Up at 1.5 m/s for a second, then down at the same speed.
    Carry(test.app, lift, [](int t) { return Math::Vec3(0.0f, t < 60 ? 1.5f : -1.5f, 0.0f); });
    for (int t = 0; t < 120; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        const float gap = FeetOf(character) - TopOf(lift);
        INFO("tick " << t << ": feet " << gap << " above the lift, ground state "
                     << static_cast<int>(body.state.ground));
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.state.groundBody == lift);
        // Descending, it trails the lift by up to one tick of its travel (the stated hover).
        REQUIRE(std::abs(gap) < restSlop + 1.5f * dt);
        INFO("it does not slip on the lift");
        REQUIRE(PositionOf(character).x == Approx(0.3f).margin(1.0e-3));
        REQUIRE(PositionOf(character).z == Approx(-0.2f).margin(1.0e-3));
        INFO("the lift's speed is in groundVelocity (" << body.state.groundVelocity.y << "), never in velocity ("
                                                       << body.velocity.y << ")");
        REQUIRE(body.state.groundVelocity.x == Approx(0.0f).margin(1.0e-4));
        REQUIRE(body.state.groundVelocity.y == Approx(t < 60 ? 1.5f : -1.5f).margin(1.0e-3));
        REQUIRE(std::abs(body.velocity.y) < 1.0e-3f);
        REQUIRE(body.state.carriedVelocity == Math::Vec3(0.0f));
    }
    INFO("it went up a metre and a half and came back down with the lift");
    REQUIRE(TopOf(lift) == Approx(0.0f).margin(1.0e-3));
}

TEST_CASE("A character standing on a descending lift never reads its speed as falling", "[physics][characters]")
{
    PhysicsApp test;
    Entity lift = AddPlatform(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    Drive(test.app, character);
    Settle(test);

    Carry(test.app, lift, [](int) { return Math::Vec3(0.0f, -1.5f, 0.0f); });
    for (int t = 0; t < 90; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        const float gap = FeetOf(character) - TopOf(lift);
        INFO("tick " << t << ": velocity.y " << body.velocity.y << ", feet " << gap << " above the lift");
        INFO("velocity.y is trimmed to 0 each tick, in the lift's frame, so game code's gravity never piles up");
        REQUIRE(std::abs(body.velocity.y) < 1.0e-3f);
        INFO("it stays on the ground, hovering at most one tick of travel above the lift (the stated limit)");
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.state.groundBody == lift);
        REQUIRE(gap > -restSlop);
        REQUIRE(gap < restSlop + 1.5f * dt);
    }
}

TEST_CASE("Where a lift turns from down to up, a character is off it for at most 4 ticks (the stated limit)",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity lift = AddPlatform(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    Drive(test.app, character);
    Settle(test);

    Carry(test.app, lift, [](int t) { return Math::Vec3(0.0f, t < 60 ? -1.5f : 1.5f, 0.0f); });
    int off = 0;
    for (int t = 0; t < 120; ++t)
    {
        test.Run(tick);
        const CharacterState& state = character.GetComponent<CharacterBody>().state;
        INFO("tick " << t << ": ground state " << static_cast<int>(state.ground));
        if (state.ground != GroundState::OnGround)
        {
            ++off;
            REQUIRE(t >= 60);
        }
    }
    INFO("off the lift for " << off << " ticks at the turn, then riding it up again");
    REQUIRE(off <= 4);
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == lift);
    REQUIRE(std::abs(FeetOf(character) - TopOf(lift)) < restSlop);
}

TEST_CASE("A character rides a shuttle that waited long enough to fall asleep", "[physics][characters]")
{
    PhysicsApp test;
    Entity shuttle = AddPlatform(test.Entities(), Math::Vec3(0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    test.Run(2.0);

    Carry(test.app, shuttle, [](int) { return Math::Vec3(2.0f, 0.0f, 0.0f); });
    test.Run(1.0);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("the shuttle moved to x " << PositionOf(shuttle).x << ", the character to " << PositionOf(character).x);
    REQUIRE(body.state.groundBody == shuttle);
    REQUIRE(PositionOf(character).x == Approx(PositionOf(shuttle).x).margin(1.0e-3));
}

TEST_CASE("A character rides a shuttle sideways without slipping", "[physics][characters]")
{
    PhysicsApp test;
    Entity shuttle = AddPlatform(test.Entities(), Math::Vec3(0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    Settle(test);

    Carry(test.app, shuttle, [](int) { return Math::Vec3(2.0f, 0.0f, 0.0f); });
    for (int t = 0; t < 90; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        const float offset = PositionOf(character).x - PositionOf(shuttle).x;
        INFO("tick " << t << ": " << offset << " along the shuttle");
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.state.groundBody == shuttle);
        REQUIRE(offset == Approx(0.0f).margin(1.0e-3));
        REQUIRE(body.state.groundVelocity.x == Approx(2.0f).margin(1.0e-3));
        REQUIRE(body.state.movedVelocity.x == Approx(2.0f).margin(1.0e-3));
        INFO("the shuttle's speed is never written into velocity");
        REQUIRE(body.velocity.x == Approx(0.0f).margin(1.0e-6));
    }
}

TEST_CASE("A jump off a lift rising at (1, 2, 0) m/s carries exactly that velocity until it lands",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity lift = AddPlatform(test.Entities(), Math::Vec3(0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    Settle(test);
    const Math::Vec3 rising(1.0f, 2.0f, 0.0f);
    Carry(test.app, lift, [rising](int) { return rising; });
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == lift);

    input->jump = true;
    test.Run(tick);
    int airborne = 0;
    while (character.GetComponent<CharacterBody>().state.ground != GroundState::OnGround)
    {
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("airborne tick " << airborne << ": carried (" << body.state.carriedVelocity.x << ", "
                              << body.state.carriedVelocity.y << ", " << body.state.carriedVelocity.z << "), velocity ("
                              << body.velocity.x << ", " << body.velocity.y << ")");
        REQUIRE(body.state.carriedVelocity.x == Approx(rising.x).margin(1.0e-4));
        REQUIRE(body.state.carriedVelocity.y == Approx(rising.y).margin(1.0e-4));
        REQUIRE(body.state.carriedVelocity.z == Approx(rising.z).margin(1.0e-4));
        INFO("none of the lift's velocity is in velocity: only the jump and game code's gravity");
        REQUIRE(body.velocity.x == Approx(0.0f).margin(1.0e-6));
        const float gravity = PhysicsWorld::Of(test.app.GetActiveScene())->Gravity().y;
        REQUIRE(body.velocity.y == Approx(jumpSpeed + static_cast<float>(airborne) * gravity * dt).margin(1.0e-3));
        REQUIRE(body.state.movedVelocity.x == Approx(rising.x).margin(1.0e-3));
        REQUIRE(++airborne < 120);
        test.Run(tick);
    }
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("it flew for " << airborne << " ticks, rising with the lift, and landed back on it");
    REQUIRE(airborne > 30);
    REQUIRE(body.state.groundBody == lift);
    INFO("landing clears carried velocity");
    REQUIRE(body.state.carriedVelocity == Math::Vec3(0.0f));
}

TEST_CASE("A character rides a dynamic raft drifting over ice", "[physics][characters]")
{
    PhysicsApp test;
    Entity ice = AddGround(test.Entities());
    ice.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = 0.0f});
    Entity raft = AddBody(test.Entities(), Math::Vec3(0.0f, 0.1f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.1f, 1.0f)});
    raft.GetComponent<RigidBody>().mass = 50.0f;
    raft.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = 0.0f});
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.2f, 0.0f));
    Drive(test.app, character);
    Settle(test);
    raft.GetComponent<RigidBody>().linearVelocity = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(tick);
    const float start = PositionOf(character).x - PositionOf(raft).x;

    for (int t = 0; t < 90; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        const float offset = PositionOf(character).x - PositionOf(raft).x;
        INFO("tick " << t << ": raft at " << PositionOf(raft).x << ", " << offset - start << " slipped");
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.state.groundBody == raft);
        REQUIRE(offset == Approx(start).margin(5.0e-3));
        REQUIRE(body.state.groundVelocity.x == Approx(raft.GetComponent<RigidBody>().linearVelocity.x).margin(1.0e-2));
    }
    INFO("the raft drifted on, and the character with it");
    REQUIRE(PositionOf(raft).x > 1.0f);
}

TEST_CASE("A character rides a turntable round without being turned, its angular velocity reported",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity turntable = AddBody(test.Entities(), Math::Vec3(0.0f, -0.1f, 0.0f), BoxShape{Math::Vec3(2.0f, 0.1f, 2.0f)},
                               MotionType::Kinematic);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(1.0f, 0.0f, 0.0f));
    Drive(test.app, character);
    Settle(test);
    const Math::Vec3 start = PositionOf(character);

    // Game code turns it about +Y at 1 rad/s.
    constexpr float spin = 1.0f;
    auto ticks = std::make_shared<int>(0);
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[ticks, turntable](App&) {
                                      turntable.GetComponent<Transform>().rot = Math::AngleAxis(
                                          spin * dt * static_cast<float>(++*ticks), Math::Vec3(0.0f, 1.0f, 0.0f));
                                  }});
    for (int t = 0; t < 90; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        const Math::Vec3 at = PositionOf(character);
        INFO("tick " << t << ": at (" << at.x << ", " << at.z << ")");
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.state.groundBody == turntable);
        INFO("its ground's spin is reported, and its velocity is the turntable's where it stands");
        REQUIRE(body.state.groundAngularVelocity.x == Approx(0.0f).margin(1.0e-3));
        REQUIRE(body.state.groundAngularVelocity.y == Approx(spin).margin(1.0e-3));
        REQUIRE(body.state.groundAngularVelocity.z == Approx(0.0f).margin(1.0e-3));
        REQUIRE(body.state.groundVelocity.x == Approx(spin * at.z).margin(2.0e-2));
        REQUIRE(body.state.groundVelocity.z == Approx(-spin * at.x).margin(2.0e-2));
        INFO("it keeps its distance from the axis");
        REQUIRE(std::sqrt(at.x * at.x + at.z * at.z) == Approx(start.x).margin(1.0e-2));
        INFO("physics never turns it: rotation is game code's");
        REQUIRE(character.GetComponent<Transform>().rot == identity);
    }
    const Math::Vec3 end = PositionOf(character);
    const float turned = 90.0f * dt * spin;
    INFO("it went round the axis with the turntable, " << turned << " rad");
    REQUIRE(end.x == Approx(start.x * std::cos(turned)).margin(2.0e-2));
    REQUIRE(end.z == Approx(-start.x * std::sin(turned)).margin(2.0e-2));
}

TEST_CASE("A teleport clears carried velocity", "[physics][characters]")
{
    PhysicsApp test;
    Entity shuttle = AddPlatform(test.Entities(), Math::Vec3(0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    Settle(test);
    Carry(test.app, shuttle, [](int) { return Math::Vec3(2.0f, 0.0f, 0.0f); });
    test.Run(0.25);

    input->jump = true;
    test.Run(tick);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::InAir);
    REQUIRE(character.GetComponent<CharacterBody>().state.carriedVelocity.x == Approx(2.0f).margin(1.0e-4));

    auto teleport = std::make_shared<bool>(true);
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[teleport, character](App&) {
                                      if (*teleport)
                                      {
                                          character.GetComponent<Transform>().pos = Math::Vec3(10.0f, 5.0f, 10.0f);
                                          *teleport = false;
                                      }
                                  }});
    test.Run(tick);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("put somewhere else in the air, it no longer carries the shuttle's speed");
    REQUIRE(body.state.carriedVelocity == Math::Vec3(0.0f));
    REQUIRE(body.state.movedVelocity.x == Approx(0.0f).margin(1.0e-4));
    REQUIRE(PositionOf(character).x == Approx(10.0f).margin(1.0e-4));
}

TEST_CASE("Steep ground is not ridden: a character on a steep slope moving along itself is left behind",
          "[physics][characters]")
{
    PhysicsApp test;
    // A 60° slope, its face across +x and up, moving along +z at 2 m/s: along its own face.
    Entity slope = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f), BoxShape{Math::Vec3(3.0f, 0.1f, 5.0f)},
                           MotionType::Kinematic);
    slope.GetComponent<Transform>().rot = Math::AngleAxis(1.0471976f, Math::Vec3(0.0f, 0.0f, 1.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    Drive(test.app, character);
    Carry(test.app, slope, [](int) { return Math::Vec3(0.0f, 0.0f, 2.0f); });

    int steep = 0;
    for (int t = 0; t < 60; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << t << ": ground state " << static_cast<int>(body.state.ground) << ", moved z "
                     << body.state.movedVelocity.z);
        REQUIRE(body.state.carriedVelocity == Math::Vec3(0.0f));
        if (body.state.ground != GroundState::OnSteepGround)
        {
            continue;
        }
        ++steep;
        REQUIRE(body.state.groundBody == slope);
        INFO("its ground's velocity is reported, but not added to the move");
        REQUIRE(body.state.groundVelocity.z == Approx(2.0f).margin(1.0e-3));
        REQUIRE(body.state.movedVelocity.z == Approx(0.0f).margin(1.0e-3));
    }
    INFO("it stood on the steep slope for " << steep << " ticks");
    REQUIRE(steep > 5);
}

TEST_CASE("Disabling a character clears carried velocity", "[physics][characters]")
{
    PhysicsApp test;
    Entity shuttle = AddPlatform(test.Entities(), Math::Vec3(0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    Settle(test);
    Carry(test.app, shuttle, [](int) { return Math::Vec3(2.0f, 0.0f, 0.0f); });
    test.Run(0.25);

    input->jump = true;
    test.Run(tick);
    REQUIRE(character.GetComponent<CharacterBody>().state.carriedVelocity.x == Approx(2.0f).margin(1.0e-4));

    character.GetComponent<CharacterBody>().enabled = false;
    test.Run(tick);
    character.GetComponent<CharacterBody>().enabled = true;
    test.Run(tick);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("re-enabled in the air, it no longer carries the shuttle's speed");
    REQUIRE(body.state.ground == GroundState::InAir);
    REQUIRE(body.state.carriedVelocity == Math::Vec3(0.0f));
    REQUIRE(body.state.movedVelocity.x == Approx(0.0f).margin(1.0e-4));
}
