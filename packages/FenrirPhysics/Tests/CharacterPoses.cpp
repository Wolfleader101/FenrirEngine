// A character body's pose: a position write is a teleport, a rotation write turns its shape without one, a parented
// character moves in world space, and between ticks it is shown blended between its last two steps.

#include <cmath>
#include <memory>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

TEST_CASE("Writing a character's position teleports it through a wall, its state from that tick's move and its "
          "velocity kept",
          "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A wall across +x, from x = 2 to x = 3, far taller than a jump.
    AddStatic(test.Entities(), Math::Vec3(2.5f, 5.0f, 0.0f), Math::Vec3(0.5f, 5.0f, 3.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);

    // Beyond the wall, 0.3 m up: within floorSnap of the ground, so a move that still thought it stood on the ground
    // where it was would stick it down to the floor.
    bool teleport = true;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                      if (teleport)
                                      {
                                          character.GetComponent<Transform>().pos =
                                              Math::Vec3(5.0f, 0.3f + footToCentre, 0.0f);
                                          teleport = false;
                                      }
                                  }});
    test.Run(tick);

    const CharacterBody& body = character.GetComponent<CharacterBody>();
    const float dt = static_cast<float>(tick);
    INFO("at x " << PositionOf(character).x << ", feet " << FeetOf(character));
    INFO("it is where it was put, past the wall, moved on by one tick's walk with no sweep from where it stood");
    REQUIRE(PositionOf(character).x == Approx(5.0f + dt).margin(1.0e-3));
    REQUIRE(FeetOf(character) > 0.25f);
    INFO("its state is that tick's move from the new place: in the air, with no ground");
    REQUIRE(body.state.ground == GroundState::InAir);
    REQUIRE_FALSE(body.state.groundBody.IsValid());
    INFO("movedVelocity is the move after the jump, not the jump");
    REQUIRE(body.state.movedVelocity.x == Approx(1.0f).margin(1.0e-3));
    INFO("its velocity is kept: game code's walk, and the falling it added");
    REQUIRE(body.velocity.x == Approx(1.0f).margin(1.0e-3));
    REQUIRE(body.velocity.y < 0.0f);
}

namespace
{
    // Writes `rotation` into the character's Transform in the next Tick, and nothing else.
    std::shared_ptr<std::optional<Math::Quat>> TurnInTick(App& app, const Entity& character)
    {
        auto turn = std::make_shared<std::optional<Math::Quat>>();
        app.AddSequentialSystems(SchedulePriority::Tick, {[turn, character](App&) {
                                     if (turn->has_value())
                                     {
                                         character.GetComponent<Transform>().rot = **turn;
                                         turn->reset();
                                     }
                                 }});
        return turn;
    }
} // namespace

TEST_CASE("A character's shape takes its rotation: laid down it rests on its side, stood up it is pushed out of the "
          "floor, on the ground both ways",
          "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    Drive(test.app, character);
    const std::shared_ptr<std::optional<Math::Quat>> turn = TurnInTick(test.app, character);
    test.Run(0.5);

    // On its side, its centre is its radius up.
    *turn = Math::AngleAxis(1.5707964f, Math::Vec3(0.0f, 0.0f, 1.0f));
    test.Run(1.0);
    {
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("lying down, its centre is at " << PositionOf(character).y);
        REQUIRE(PositionOf(character).y == Approx(0.3f).margin(restSlop));
        REQUIRE(PositionOf(character).x == Approx(0.0f).margin(1.0e-3));
        INFO("up is still world +Y, and its side, the bottom of its turned shape, counts as ground");
        REQUIRE(body.state.ground == GroundState::OnGround);
        REQUIRE(body.state.groundNormal.y == Approx(1.0f).margin(1.0e-4));
    }

    // Upright again, half a metre of it is in the floor.
    *turn = identity;
    test.Run(1.0);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("stood up, the moves pushed it out of the floor: its centre is at " << PositionOf(character).y);
    REQUIRE(PositionOf(character).y == Approx(footToCentre).margin(restSlop));
    REQUIRE(body.state.ground == GroundState::OnGround);
    INFO("the push out of the floor is not upward speed in its velocity");
    REQUIRE(std::abs(body.velocity.y) < 1.0e-3f);
}

TEST_CASE("A parented character moves in world space, its local Transform written back against its parent; moving "
          "the parent teleports it",
          "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A wall across +z, from z = 7 to z = 8.
    AddStatic(test.Entities(), Math::Vec3(0.0f, 5.0f, 7.5f), Math::Vec3(9.0f, 5.0f, 0.5f));
    // Turned a quarter about +Y: its local +x is world -z, and its local +z world +x.
    Entity parent = Place(test.Entities(), Math::Vec3(1.0f, 0.0f, 5.0f));
    parent.GetComponent<Transform>().rot = Math::AngleAxis(1.5707964f, Math::Vec3(0.0f, 1.0f, 0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    parent.AddChild(character);
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);

    test.Run(1.0);
    const Math::Vec3 world = PositionOf(character);
    INFO("world (" << world.x << ", " << world.y << ", " << world.z << ")");
    INFO("it fell to the floor and walked a metre along world +x, whatever its parent's turn");
    REQUIRE(world.y == Approx(footToCentre).margin(restSlop));
    REQUIRE(world.x == Approx(2.0f).margin(0.02));
    REQUIRE(world.z == Approx(5.0f).margin(1.0e-3));
    const Transform& local = character.GetComponent<Transform>();
    INFO("local (" << local.pos.x << ", " << local.pos.y << ", " << local.pos.z << ")");
    INFO("its local Transform is that place against its parent's turned pose, and its local rotation untouched");
    REQUIRE(local.pos.x == Approx(-(world.z - 5.0f)).margin(1.0e-4));
    REQUIRE(local.pos.y == Approx(world.y).margin(1.0e-4));
    REQUIRE(local.pos.z == Approx(world.x - 1.0f).margin(1.0e-4));
    REQUIRE(local.rot == identity);

    // Game code moves the parent 4 m along +z, past the wall.
    bool move = true;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                      if (move)
                                      {
                                          parent.GetComponent<Transform>().pos.z += 4.0f;
                                          move = false;
                                      }
                                  }});
    test.Run(tick);
    const Math::Vec3 moved = PositionOf(character);
    INFO("after the parent moved, world (" << moved.x << ", " << moved.y << ", " << moved.z << ")");
    INFO("it went with its parent, through the wall with no sweep, and walked on from there");
    REQUIRE(moved.z == Approx(9.0f).margin(1.0e-3));
    REQUIRE(moved.x == Approx(world.x + static_cast<float>(tick)).margin(1.0e-3));
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
    REQUIRE(character.GetComponent<CharacterBody>().state.movedVelocity.z == Approx(0.0f).margin(1.0e-3));
}

namespace
{
    constexpr float yawPerTick = 0.05f;

    // Turns the character about +Y by yawPerTick each tick, as game code turns a character in Tick.
    void TurnEachTick(App& app, const Entity& character)
    {
        auto yaw = std::make_shared<float>(0.0f);
        app.AddSequentialSystems(SchedulePriority::Tick, {[yaw, character](App&) {
                                     *yaw += yawPerTick;
                                     character.GetComponent<Transform>().rot =
                                         Math::AngleAxis(*yaw, Math::Vec3(0.0f, 1.0f, 0.0f));
                                 }});
    }

    // The angle about +Y of a rotation about +Y.
    float YawOf(const Math::Quat& rotation)
    {
        return 2.0f * std::atan2(rotation.y, rotation.w);
    }

    // A character walking along +x at 2 m/s on the ground, turning each tick if `turning`, shown blended between
    // ticks unless `config` or `interpolate` turns that off.
    struct WalkingCharacter
    {
        explicit WalkingCharacter(const PhysicsConfig& config, bool interpolate = true, bool turning = true)
            : test(config)
        {
            AddGround(test.Entities());
            character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
            character.GetComponent<CharacterBody>().interpolate = interpolate;
            input = Drive(test.app, character);
            if (turning)
            {
                TurnEachTick(test.app, character);
            }
            steps = RecordStepPoses(test.app, character);
            test.Run(0.5);
            input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
            test.Run(0.25);
        }

        PhysicsApp test;
        Entity character;
        std::shared_ptr<Input> input;
        Poses steps;
    };
} // namespace

TEST_CASE("Between ticks a walking, turning character is shown blended between its last two step poses",
          "[physics][characters][interpolation]")
{
    WalkingCharacter walking{PhysicsConfig{}};
    const Poses& steps = walking.steps;

    int blended = 0;
    for (int frame = 0; frame < 40; ++frame)
    {
        walking.test.app.RunFrame(shortFrame);
        const float fraction = FractionOf(walking.test.app);
        const Transform& from = (*steps)[steps->size() - 2];
        const Transform& to = steps->back();
        const Math::Vec3 expected = Blend(from.pos, to.pos, fraction);
        const float yaw = YawOf(from.rot) + (YawOf(to.rot) - YawOf(from.rot)) * fraction;
        const Math::Quat expectedRotation = Math::AngleAxis(yaw, Math::Vec3(0.0f, 1.0f, 0.0f));
        const Transform shown = GetWorldTransform(walking.character);
        INFO("frame " << frame << ": " << fraction << " of the way from x = " << from.pos.x << " to x = " << to.pos.x
                      << " is x = " << expected.x << ", shown at x = " << shown.pos.x << "; yaw " << yaw
                      << " expected, " << YawOf(shown.rot) << " shown");
        REQUIRE(shown.pos.x == Approx(expected.x).margin(1e-4));
        REQUIRE(shown.pos.y == Approx(expected.y).margin(1e-4));
        REQUIRE(shown.pos.z == Approx(expected.z).margin(1e-4));
        REQUIRE(shown.rot.w == Approx(expectedRotation.w).margin(1e-4));
        REQUIRE(shown.rot.y == Approx(expectedRotation.y).margin(1e-4));
        if (fraction > 0.05f)
        {
            ++blended;
            // A turn every tick is not a teleport: the position still blends.
            REQUIRE(shown.pos.x < to.pos.x);
            REQUIRE(shown.pos.x > from.pos.x);
        }
    }
    INFO("most frames end part of the way through a tick");
    REQUIRE(blended > 20);
}

TEST_CASE("A teleported character is shown where it was put, never blended across the jump",
          "[physics][characters][interpolation]")
{
    WalkingCharacter walking{PhysicsConfig{}};
    bool teleport = false;
    walking.test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                              if (teleport)
                                              {
                                                  walking.character.GetComponent<Transform>().pos =
                                                      Math::Vec3(-5.0f, footToCentre, 3.0f);
                                                  teleport = false;
                                              }
                                          }});
    walking.test.app.RunFrame(shortFrame);
    REQUIRE(PositionOf(walking.character).x > 0.4f);

    teleport = true;
    while (teleport)
    {
        walking.test.app.RunFrame(shortFrame);
    }
    for (int frame = 0; frame < 20; ++frame)
    {
        const Math::Vec3 shown = PositionOf(walking.character);
        INFO("frame " << frame << " after the write: shown at " << shown.x << ", " << shown.z
                      << "; it never blends from where it walked (x > 0.4, z = 0) to where it was put (-5, 3)");
        REQUIRE(shown.z == Approx(3.0f).margin(1e-4));
        REQUIRE(shown.x >= -5.0f);
        REQUIRE(shown.x < -4.0f);
        walking.test.app.RunFrame(shortFrame);
    }
}

TEST_CASE("A new character is not blended from the origin", "[physics][characters][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddGround(test.Entities());
    test.Run(0.25);

    Entity character = AddCharacter(test.Entities(), Math::Vec3(3.0f, 0.0f, 2.0f));
    for (int frame = 0; frame < 20; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const Math::Vec3 shown = PositionOf(character);
        INFO("frame " << frame << ": shown at " << shown.x << ", " << shown.y << ", " << shown.z);
        REQUIRE(shown.x == Approx(3.0f).margin(1e-4));
        REQUIRE(shown.z == Approx(2.0f).margin(1e-4));
        REQUIRE(shown.y == Approx(footToCentre).margin(restSlop));
    }
}

TEST_CASE("A character with interpolate off, or under the app-wide switch, is shown at its step poses only",
          "[physics][characters][interpolation]")
{
    PhysicsConfig appWideOff = StepPosesOnly();
    WalkingCharacter off{PhysicsConfig{}, false};
    WalkingCharacter switchedOff{appWideOff};

    for (WalkingCharacter* walking : {&off, &switchedOff})
    {
        for (int frame = 0; frame < 20; ++frame)
        {
            walking->test.app.RunFrame(shortFrame);
            const Transform& step = walking->steps->back();
            const Transform shown = GetWorldTransform(walking->character);
            INFO((walking == &off ? "interpolate off" : "the app-wide switch off")
                 << ", frame " << frame << ": the last step left it at x = " << step.pos.x
                 << ", shown at x = " << shown.pos.x);
            REQUIRE(shown.pos.x == step.pos.x);
            REQUIRE(shown.rot.w == step.rot.w);
            REQUIRE(shown.rot.y == step.rot.y);
        }
    }
}

TEST_CASE("Turning a blended character in Update is not a teleport: it walks on from its true position",
          "[physics][characters][interpolation]")
{
    WalkingCharacter walking{PhysicsConfig{}, true, false};
    bool turn = false;
    walking.test.app.AddSequentialSystems(SchedulePriority::Update, {[&](App&) {
                                              if (turn)
                                              {
                                                  walking.character.GetComponent<Transform>().rot =
                                                      Math::AngleAxis(2.0f, Math::Vec3(0.0f, 1.0f, 0.0f));
                                                  turn = false;
                                              }
                                          }});
    // Part of the way through a tick, so the character is shown behind where it is.
    while (FractionOf(walking.test.app) < 0.5f)
    {
        walking.test.app.RunFrame(shortFrame);
    }
    const std::size_t stepsBefore = walking.steps->size();
    const float trueX = walking.steps->back().pos.x;
    REQUIRE(PositionOf(walking.character).x < trueX - 0.01f);

    turn = true;
    while (walking.steps->size() == stepsBefore)
    {
        walking.test.app.RunFrame(shortFrame);
    }
    const Transform step = walking.steps->back();
    INFO("its true x was " << trueX << "; the step after the turn left it at x = " << step.pos.x);
    REQUIRE(step.pos.x == Approx(trueX + 2.0f * static_cast<float>(tick)).margin(1e-4));
    INFO("and the step took the turn");
    REQUIRE(YawOf(step.rot) == Approx(2.0f).margin(1e-4));
}
