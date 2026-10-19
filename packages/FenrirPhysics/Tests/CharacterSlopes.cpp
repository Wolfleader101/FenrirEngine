// A character walks a level without tuning: up a ramp but not a cliff, up and down stairs without leaving the ground,
// sticking to the ground going down, and standing still on walkable slopes unless told to slide.

#include <algorithm>
#include <cmath>
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
    constexpr float ramp = 0.5235988f;  // 30°
    constexpr float steep = 1.0471976f; // 60°

    // A static slope rising towards +x at `angle`, its face through the origin: at x its surface is x tan(angle) high,
    // for 5 m along the face either side of the origin.
    Entity AddSlope(EntityList& entities, float angle)
    {
        const Math::Quat rotation = Math::AngleAxis(angle, Math::Vec3(0.0f, 0.0f, 1.0f));
        Entity slope = Place(entities, rotation * Math::Vec3(0.0f, -0.5f, 0.0f));
        slope.GetComponent<Transform>().rot = rotation;
        slope.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(5.0f, 0.5f, 3.0f)}});
        return slope;
    }

    float SurfaceHeight(float angle, float x)
    {
        return x * std::tan(angle);
    }

    // Ten steps 0.3 m high and 0.5 m deep rising towards +x from x = 0, then a ledge 3 m high from x = 5 to x = 8.
    constexpr int stepCount = 10;
    constexpr float stepRise = 0.3f;
    constexpr float stepDepth = 0.5f;
    constexpr float ledgeTop = stepCount * stepRise;

    void AddStairs(EntityList& entities)
    {
        for (int i = 0; i < stepCount; ++i)
        {
            const float top = static_cast<float>(i + 1) * stepRise;
            AddStatic(entities, Math::Vec3((static_cast<float>(i) + 0.5f) * stepDepth, 0.5f * top, 0.0f),
                      Math::Vec3(0.5f * stepDepth, 0.5f * top, 1.0f));
        }
        AddStatic(entities, Math::Vec3(stepCount * stepDepth + 1.5f, 0.5f * ledgeTop, 0.0f),
                  Math::Vec3(1.5f, 0.5f * ledgeTop, 1.0f));
    }

    // Game code that steers rather than sets: input is added to the written-back velocity, so a slide keeps its speed.
    void DriveAdding(App& app, const Entity& character)
    {
        app.AddSequentialSystems(SchedulePriority::Tick, {[character](App& running) {
                                     const PhysicsWorld* world = PhysicsWorld::Of(running.GetActiveScene());
                                     if (world == nullptr)
                                     {
                                         return;
                                     }
                                     character.GetComponent<CharacterBody>().velocity.y += world->Gravity().y * dt;
                                 }});
    }

    float Speed(const Math::Vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }
} // namespace

TEST_CASE("A character walks up a 30° slope, and with stopOnSlope stands still on it", "[physics][characters][slopes]")
{
    PhysicsApp test;
    AddSlope(test.Entities(), ramp);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-1.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);

    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    const float startX = PositionOf(character).x;
    for (int t = 0; t < 60; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << t << " walking up: ground " << static_cast<int>(body.state.ground) << ", velocity.y "
                     << body.velocity.y);
        REQUIRE(body.state.ground == GroundState::OnGround);
        // Walking up a slope never turns into climbing speed in velocity.
        REQUIRE(body.velocity.y <= 1.0e-3f);
    }
    const float walked = PositionOf(character).x - startX;
    INFO("it walked " << walked << " m along x in a second");
    // Jolt walks the horizontal speed along the face: 2 m/s horizontal up 30° is 1.5 m/s along x.
    REQUIRE(walked > 1.2f);
    const CharacterState& state = character.GetComponent<CharacterBody>().state;
    REQUIRE(state.groundNormal.x == Approx(-std::sin(ramp)).margin(1.0e-3));
    REQUIRE(state.groundNormal.y == Approx(std::cos(ramp)).margin(1.0e-3));

    input->walk = Math::Vec3(0.0f);
    test.Run(0.25);
    const Math::Vec3 stopped = PositionOf(character);
    test.Run(2.0);
    const Math::Vec3 after = PositionOf(character);
    INFO("standing for 2 s it moved " << Speed(after - stopped) << " m");
    REQUIRE(Speed(after - stopped) < 1.0e-3f);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
}

TEST_CASE("With stopOnSlope off a character slides down a 30° slope, keeping its speed when game code adds to it",
          "[physics][characters][slopes]")
{
    PhysicsApp test;
    AddSlope(test.Entities(), ramp);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(3.0f, SurfaceHeight(ramp, 3.0f) + 0.2f, 0.0f));
    character.GetComponent<CharacterBody>().stopOnSlope = false;
    DriveAdding(test.app, character);
    test.Run(0.25);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
    const float startX = PositionOf(character).x;

    test.Run(1.0);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("after a second on the slope: at x " << PositionOf(character).x << ", velocity (" << body.velocity.x << ", "
                                              << body.velocity.y << ", " << body.velocity.z << ")");
    REQUIRE(PositionOf(character).x < startX - 1.0f);
    REQUIRE(body.state.ground == GroundState::OnGround);
    const Math::Vec3 moved = body.state.movedVelocity;
    INFO("moving at (" << moved.x << ", " << moved.y << ", " << moved.z << ")");
    REQUIRE(Speed(moved) > 4.0f);
    REQUIRE(moved.y / moved.x == Approx(std::tan(ramp)).epsilon(0.05));
}

TEST_CASE("Game code setting horizontal speed every tick throws a slide's speed away: the character only creeps",
          "[physics][characters][slopes]")
{
    PhysicsApp test;
    AddSlope(test.Entities(), ramp);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(3.0f, SurfaceHeight(ramp, 3.0f) + 0.2f, 0.0f));
    character.GetComponent<CharacterBody>().stopOnSlope = false;
    Drive(test.app, character);
    test.Run(0.25);
    const float startX = PositionOf(character).x;

    test.Run(1.0);
    INFO("it crept " << startX - PositionOf(character).x << " m in a second");
    REQUIRE(PositionOf(character).x < startX);
    REQUIRE(PositionOf(character).x > startX - 0.5f);
}

TEST_CASE("A 60° slope is not climbed: on it the character reads OnSteepGround, and slides down to its foot",
          "[physics][characters][slopes]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity slope = AddSlope(test.Entities(), steep);
    // Dropped onto the face 1 m up, walking up it.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.6f, SurfaceHeight(steep, 0.6f) + 0.4f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    while (character.GetComponent<CharacterBody>().state.ground == GroundState::InAir)
    {
        test.Run(tick);
    }

    // Game code setting its horizontal speed each tick throws the slide's speed away, so it creeps down.
    int steepTicks = 0;
    float lowest = FeetOf(character);
    for (int t = 0; t < 600 && character.GetComponent<CharacterBody>().state.ground != GroundState::OnGround; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << t << ": feet at " << FeetOf(character) << ", ground " << static_cast<int>(body.state.ground)
                     << ", velocity.y " << body.velocity.y);
        REQUIRE(FeetOf(character) <= lowest + 1.0e-4f);
        lowest = std::min(lowest, FeetOf(character));
        REQUIRE(body.velocity.y <= 1.0e-3f);
        if (body.state.ground != GroundState::OnSteepGround)
        {
            continue;
        }
        ++steepTicks;
        INFO("on steep ground, the ground fields name the slope and its face's normal");
        REQUIRE(body.state.groundBody == slope);
        REQUIRE(body.state.groundCollider == slope);
        REQUIRE(body.state.groundNormal.x == Approx(-std::sin(steep)).margin(1.0e-3));
        REQUIRE(body.state.groundNormal.y == Approx(std::cos(steep)).margin(1.0e-3));
    }
    INFO("it stood on the slope for " << steepTicks << " ticks");
    REQUIRE(steepTicks > 60);

    test.Run(1.0);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("it ends at the foot, still walking into the slope, on the ground, its feet at " << FeetOf(character));
    REQUIRE(body.state.ground == GroundState::OnGround);
    REQUIRE(body.state.groundBody == ground);
    REQUIRE(FeetOf(character) < 0.35f);
    REQUIRE(body.state.onWall);
}

TEST_CASE("maxSlope 0 turns slope limiting off at the next move: a 60° slope is ground to stand on",
          "[physics][characters][slopes]")
{
    PhysicsApp test;
    AddSlope(test.Entities(), steep);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(1.5f, SurfaceHeight(steep, 1.5f) + 0.4f, 0.0f));
    Drive(test.app, character);
    while (character.GetComponent<CharacterBody>().state.ground != GroundState::OnSteepGround)
    {
        test.Run(tick);
    }

    character.GetComponent<CharacterBody>().maxSlope = 0.0f;
    test.Run(tick);
    INFO("the tick after, it stands on the slope");
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
    test.Run(0.25);
    const Math::Vec3 stopped = PositionOf(character);
    test.Run(1.0);
    INFO("with stopOnSlope it stays put: moved " << Speed(PositionOf(character) - stopped) << " m");
    REQUIRE(Speed(PositionOf(character) - stopped) < 1.0e-3f);

    character.GetComponent<CharacterBody>().maxSlope = 0.785f;
    test.Run(tick);
    INFO("given its limit back, it is on steep ground again the tick after");
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnSteepGround);
}

TEST_CASE("A character against a ledge with nothing underneath is Touching, with no ground",
          "[physics][characters][slopes]")
{
    PhysicsApp test;
    // A ledge whose face is at x = 2, its top 4 m up.
    AddStatic(test.Entities(), Math::Vec3(2.5f, 2.0f, 0.0f), Math::Vec3(0.5f, 2.0f, 2.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(2.0f - 0.3f - 0.05f, 2.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);

    test.Run(tick * 5);
    const CharacterState& state = character.GetComponent<CharacterBody>().state;
    INFO("falling against the ledge's face, ground " << static_cast<int>(state.ground));
    REQUIRE(state.ground == GroundState::Touching);
    REQUIRE(state.onWall);
    REQUIRE_FALSE(state.groundBody.IsValid());
    REQUIRE_FALSE(state.groundCollider.IsValid());
    REQUIRE(state.groundPoint == Math::Vec3(0.0f));
    REQUIRE(state.groundNormal == Math::Vec3(0.0f));
    REQUIRE(state.groundVelocity == Math::Vec3(0.0f));
}

TEST_CASE("A character climbs 0.3 m stairs never leaving the ground, with no upward speed in velocity, the step-ups in "
          "movedVelocity",
          "[physics][characters][stairs]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    AddStairs(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-1.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.25);

    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    float risen = 0.0f;
    const float startFeet = FeetOf(character);
    int ticks = 0;
    for (; ticks < 300 && FeetOf(character) < ledgeTop - restSlop; ++ticks)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << ticks << ": feet at " << FeetOf(character) << ", x " << PositionOf(character).x << ", ground "
                     << static_cast<int>(body.state.ground) << ", velocity.y " << body.velocity.y);
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.velocity.y <= 1.0e-3f);
        risen += body.state.movedVelocity.y * dt;
    }
    INFO("it took " << ticks << " ticks to reach the ledge");
    REQUIRE(FeetOf(character) >= ledgeTop - restSlop);
    INFO("movedVelocity carried every step-up: it rose " << risen << " m by it, " << FeetOf(character) - startFeet
                                                         << " m in fact");
    REQUIRE(risen == Approx(FeetOf(character) - startFeet).margin(1.0e-3));
}

TEST_CASE("A character walks down rounded-edged 0.3 m stairs on the ground every tick, never on steep ground",
          "[physics][characters][stairs]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    AddStairs(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(6.5f, ledgeTop, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.25);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);

    input->walk = Math::Vec3(-4.0f, 0.0f, 0.0f);
    int ticks = 0;
    for (; ticks < 300 && FeetOf(character) > restSlop; ++ticks)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << ticks << ": feet at " << FeetOf(character) << ", x " << PositionOf(character).x << ", ground "
                     << static_cast<int>(body.state.ground) << ", normal (" << body.state.groundNormal.x << ", "
                     << body.state.groundNormal.y << ")");
        REQUIRE(body.state.ground == GroundState::OnGround);
        // The steps' faces, not their rounded edges.
        REQUIRE(body.state.groundNormal.y == Approx(1.0f).margin(1.0e-4));
    }
    INFO("it took " << ticks << " ticks to reach the bottom");
    REQUIRE(FeetOf(character) <= restSlop);
}

TEST_CASE("stepHeight 0 turns stair walking off at the next move: the first step blocks the character until it is "
          "back",
          "[physics][characters][stairs]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    AddStairs(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-1.0f, 0.0f, 0.0f));
    character.GetComponent<CharacterBody>().stepHeight = 0.0f;
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.25);

    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    test.Run(1.0);
    INFO("blocked at the first riser: x " << PositionOf(character).x << ", feet " << FeetOf(character));
    REQUIRE(FeetOf(character) < restSlop);
    REQUIRE(PositionOf(character).x < -0.3f + restSlop);

    character.GetComponent<CharacterBody>().stepHeight = 0.4f;
    test.Run(tick);
    INFO("the tick after it is back, it starts up the step: feet at " << FeetOf(character));
    REQUIRE(FeetOf(character) > 0.1f);
    test.Run(tick * 5);
    INFO("and is on it a few ticks later: feet at " << FeetOf(character));
    REQUIRE(FeetOf(character) > stepRise - restSlop);
}

TEST_CASE("floorSnap 0 turns sticking to the ground off at the next move: walking down stairs leaves the ground",
          "[physics][characters][stairs]")
{
    const auto airborneTicks = [](bool snap) {
        PhysicsApp test;
        AddGround(test.Entities());
        AddStairs(test.Entities());
        Entity character = AddCharacter(test.Entities(), Math::Vec3(6.5f, ledgeTop, 0.0f));
        std::shared_ptr<Input> input = Drive(test.app, character);
        test.Run(0.25);
        input->walk = Math::Vec3(-4.0f, 0.0f, 0.0f);
        test.Run(tick * 6);
        // Switched while it walks along the ledge, before it reaches the stairs.
        character.GetComponent<CharacterBody>().floorSnap = snap ? 0.5f : 0.0f;
        int airborne = 0;
        for (int t = 0; t < 120; ++t)
        {
            test.Run(tick);
            airborne += character.GetComponent<CharacterBody>().state.ground == GroundState::InAir ? 1 : 0;
        }
        INFO("down to feet at " << FeetOf(character));
        REQUIRE(FeetOf(character) < restSlop);
        return airborne;
    };
    REQUIRE(airborneTicks(true) == 0);
    REQUIRE(airborneTicks(false) > 10);
}
