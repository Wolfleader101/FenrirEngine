// PROTOTYPE (prototype/fenrir-physics-character-spike, ticket 37): headless checks of the proposed character body,
// beside the hand checks in the DemoApp's "Characters (spike)" scene. Run with FenrirPhysicsTests "[character-spike]".

#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/PhysicsWorld.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;

namespace
{
    // A capsule 1.6 m tall, 0.3 m round: its centre 0.8 m above its feet.
    constexpr float footToCentre = 0.8f;

    Entity AddCharacter(EntityList& entities, const Math::Vec3& feet)
    {
        Entity character = Place(entities, feet + Math::Vec3(0.0f, footToCentre, 0.0f));
        character.AddComponent<Collider>(Collider{CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f}});
        character.AddComponent<CharacterBody>();
        return character;
    }

    Entity AddStatic(EntityList& entities, const Math::Vec3& centre, const Math::Vec3& halfExtents,
                     const Math::Quat& rotation = identity)
    {
        Entity block = Place(entities, centre);
        block.GetComponent<Transform>().rot = rotation;
        block.AddComponent<Collider>(Collider{BoxShape{halfExtents}});
        return block;
    }

    // Game code's input: a walking velocity and a jump, read each tick.
    struct Input
    {
        Math::Vec3 walk = Math::Vec3(0.0f);
        bool jump = false;
        float jumpSpeed = 5.0f;
        // Keep the horizontal velocity physics wrote back instead of setting it from `walk`: only then can a slide
        // build up speed.
        bool keepHorizontal = false;
    };

    // The game code the spec asks for, in Tick: horizontal speed from input, gravity added, a jump only from walkable
    // ground. What physics wrote back stays in velocity.y.
    std::shared_ptr<Input> Drive(App& app, const Entity& character)
    {
        auto input = std::make_shared<Input>();
        app.AddSequentialSystems(SchedulePriority::Tick, {[input, character](App& running) {
                                     PhysicsWorld* world = PhysicsWorld::Of(running.GetActiveScene());
                                     if (world == nullptr)
                                     {
                                         return;
                                     }
                                     const float dt = static_cast<float>(running.GetTime().tickRate);
                                     CharacterBody& body = character.GetComponent<CharacterBody>();
                                     if (!input->keepHorizontal)
                                     {
                                         body.velocity.x = input->walk.x;
                                         body.velocity.z = input->walk.z;
                                     }
                                     body.velocity.y += world->Gravity().y * dt;
                                     if (input->jump &&
                                         world->GetCharacterState(character).ground == CharacterGround::OnGround)
                                     {
                                         body.velocity.y = input->jumpSpeed;
                                         input->jump = false;
                                     }
                                 }});
        return input;
    }

    CharacterState StateOf(PhysicsApp& test, const Entity& character)
    {
        return PhysicsWorld::Of(test.app.GetActiveScene())->GetCharacterState(character);
    }

    float FeetOf(const Entity& character)
    {
        return PositionOf(character).y - footToCentre;
    }

    // A kinematic lift 4 m square and 0.2 m thick, its top at `top`, moved by game code in Tick at `speed` for
    // `seconds` and then back, over and over.
    Entity AddLift(PhysicsApp& test, const Math::Vec3& top, const Math::Vec3& speed, double seconds)
    {
        Entity lift = AddBody(test.Entities(), top - Math::Vec3(0.0f, 0.1f, 0.0f),
                              BoxShape{Math::Vec3(2.0f, 0.1f, 2.0f)}, MotionType::Kinematic);
        auto time = std::make_shared<double>(0.0);
        test.app.AddSequentialSystems(SchedulePriority::Tick, {[lift, speed, seconds, time](App& running) {
                                          const double dt = running.GetTime().tickRate;
                                          const bool out = std::fmod(*time, 2.0 * seconds) < seconds;
                                          lift.GetComponent<Transform>().pos +=
                                              speed * static_cast<float>(out ? dt : -dt);
                                          *time += dt;
                                      }});
        return lift;
    }
} // namespace

TEST_CASE("A character lands and stands, its falling speed cleared each tick", "[character-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    Drive(test.app, character);

    test.Run(1.5);
    for (int t = 0; t < 60; ++t)
    {
        test.Run(t == 0 ? 0.0 : PhysicsTest::tick);
        INFO("tick " << t << ": feet at " << FeetOf(character) << ", velocity.y "
                     << character.GetComponent<CharacterBody>().velocity.y);
        REQUIRE(StateOf(test, character).ground == CharacterGround::OnGround);
        REQUIRE(std::abs(FeetOf(character)) < restSlop);
        // At most this tick's gravity: the trim cleared the last tick's.
        REQUIRE(std::abs(character.GetComponent<CharacterBody>().velocity.y) < 1.0e-3f);
    }
}

TEST_CASE("A ceiling clears a jump's upward speed", "[character-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // Its underside 2.0 m up: 0.4 m above the character's head.
    AddStatic(test.Entities(), Math::Vec3(0.0f, 2.25f, 0.0f), Math::Vec3(2.0f, 0.25f, 2.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);

    input->jump = true;
    float highest = 0.0f;
    bool cleared = false;
    for (int t = 0; t < 30 && !cleared; ++t)
    {
        test.Run(PhysicsTest::tick);
        highest = std::max(highest, FeetOf(character));
        cleared = highest > 0.3f && character.GetComponent<CharacterBody>().velocity.y <= 0.0f;
    }
    INFO("the head reached " << highest + 2.0f * footToCentre << " m");
    REQUIRE(highest + 2.0f * footToCentre < 2.0f + restSlop);
    REQUIRE(cleared);
}

TEST_CASE("stopOnSlope holds a character still on a walkable slope; off, it slides", "[character-spike]")
{
    for (const bool stop : {true, false})
    {
        PhysicsApp test;
        // 30 degrees, rising along +x.
        const Math::Quat tilt = Math::AngleAxis(std::numbers::pi_v<float> / 6.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
        AddStatic(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f), Math::Vec3(6.0f, 0.5f, 3.0f), tilt);
        Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        character.GetComponent<CharacterBody>().stopOnSlope = stop;
        // Game code that sets horizontal velocity from input every tick would throw a slide's speed away.
        Drive(test.app, character)->keepHorizontal = true;
        test.Run(1.0);
        const Math::Vec3 settled = PositionOf(character);
        test.Run(2.0);
        const float drift = Math::Magnitude(PositionOf(character) - settled);
        INFO("stopOnSlope " << stop << ": drifted " << drift << " m in 2 s, ground "
                            << static_cast<int>(StateOf(test, character).ground));
        if (stop)
        {
            REQUIRE(drift < 1.0e-3f);
            REQUIRE(StateOf(test, character).ground == CharacterGround::OnGround);
        }
        else
        {
            REQUIRE(drift > 0.5f);
        }
    }
}

TEST_CASE("A character cannot walk up a slope steeper than maxSlope", "[character-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // 60 degrees, its foot at x = 2, rising along +x.
    const Math::Quat tilt = Math::AngleAxis(std::numbers::pi_v<float> / 3.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
    AddStatic(test.Entities(), Math::Vec3(4.0f, 0.0f, 0.0f), Math::Vec3(3.0f, 0.2f, 3.0f), tilt);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(3.0f, 0.0f, 0.0f);
    test.Run(3.0);
    INFO("feet at " << FeetOf(character) << " m after 3 s walking into it");
    REQUIRE(FeetOf(character) < 0.4f);
}

TEST_CASE("Walking up stairs never shows in velocity", "[character-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // Eight 0.25 m steps, each 0.4 m deep, from x = 1.
    for (int i = 0; i < 8; ++i)
    {
        const float rise = 0.25f * static_cast<float>(i + 1);
        AddStatic(test.Entities(), Math::Vec3(1.2f + 0.4f * static_cast<float>(i), rise / 2.0f, 0.0f),
                  Math::Vec3(0.2f, rise / 2.0f, 2.0f));
    }
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    float worstUp = 0.0f;
    float worstAlong = 0.0f;
    int notOnGround = 0;
    for (int t = 0; t < 120; ++t)
    {
        test.Run(PhysicsTest::tick);
        const Math::Vec3 velocity = character.GetComponent<CharacterBody>().velocity;
        worstUp = std::max(worstUp, velocity.y);
        if (PositionOf(character).x < 3.5f)
        {
            worstAlong = std::max(worstAlong, std::abs(velocity.x - 2.0f));
        }
        notOnGround += StateOf(test, character).ground == CharacterGround::OnGround ? 0 : 1;
    }
    INFO("feet at " << FeetOf(character) << ", x " << PositionOf(character).x << "; most upward velocity " << worstUp
                    << ", most walking speed lost " << worstAlong << ", ticks off the ground " << notOnGround);
    REQUIRE(FeetOf(character) > 1.9f);
    REQUIRE(worstUp < 1.0e-3f);
    // Found: for a tick or two at each step, its rounded edge reads as a slope too steep to stand on, and the trim
    // takes about a tenth of the walking speed into it.
    REQUIRE(worstAlong < 0.25f);
    REQUIRE(notOnGround == 0);
}

TEST_CASE("A character rides a lift up and down, and sideways, staying on it", "[character-spike]")
{
    struct Case
    {
        const char* name;
        Math::Vec3 speed;
    };
    for (const Case& each :
         {Case{"vertical", Math::Vec3(0.0f, 2.0f, 0.0f)}, Case{"sideways", Math::Vec3(2.0f, 0.0f, 0.0f)}})
    {
        PhysicsApp test;
        AddGround(test.Entities());
        Entity lift = AddLift(test, Math::Vec3(0.0f, 0.5f, 0.0f), each.speed, 1.0);
        Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.6f, 0.0f));
        Drive(test.app, character);
        test.Run(0.25); // landed, the lift moving

        float worstGap = 0.0f;
        float worstSlip = 0.0f;
        int notOnLift = 0;
        const float offset = PositionOf(character).x - PositionOf(lift).x;
        for (int t = 0; t < 240; ++t)
        {
            test.Run(PhysicsTest::tick);
            const float top = PositionOf(lift).y + 0.1f;
            worstGap = std::max(worstGap, std::abs(FeetOf(character) - top));
            worstSlip = std::max(worstSlip, std::abs(PositionOf(character).x - PositionOf(lift).x - offset));
            const CharacterState state = StateOf(test, character);
            notOnLift += state.ground == CharacterGround::OnGround && state.groundBody == lift ? 0 : 1;
        }
        INFO(each.name << ": worst gap to the lift " << worstGap << " m, worst slip " << worstSlip
                       << " m, ticks not on it " << notOnLift);
        // Found: moved before the step, it trails a descending lift by one tick of travel (Jolt's documented hover:
        // 2 m/s / 60 Hz = 3.3 cm), and where the lift turns from down to up that gap leaves it in the air for 4 ticks
        // (twice in these 4 s).
        REQUIRE(worstGap < 2.0f / 60.0f + 0.005f);
        REQUIRE(worstSlip < 0.01f);
        REQUIRE(notOnLift <= (each.speed.y > 0.0f ? 8 : 0));
    }
}

TEST_CASE("Jumping off a rising lift carries its velocity until landing", "[character-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity lift = AddLift(test, Math::Vec3(0.0f, 0.5f, 0.0f), Math::Vec3(1.0f, 2.0f, 0.0f), 2.0);
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.6f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);

    input->jump = true;
    test.Run(3.0 * PhysicsTest::tick);
    const CharacterState inAir = StateOf(test, character);
    INFO("carried " << inAir.carriedVelocity.x << ", " << inAir.carriedVelocity.y << "; ground "
                    << static_cast<int>(inAir.ground));
    REQUIRE(inAir.ground == CharacterGround::InAir);
    REQUIRE(std::abs(inAir.carriedVelocity.x - 1.0f) < 1.0e-3f);
    REQUIRE(std::abs(inAir.carriedVelocity.y - 2.0f) < 1.0e-3f);
    // Not game code's: velocity.y holds the jump less gravity, and no lift speed.
    REQUIRE(std::abs(character.GetComponent<CharacterBody>().velocity.x) < 1.0e-6f);

    test.Run(2.0);
    REQUIRE(StateOf(test, character).ground == CharacterGround::OnGround);
    REQUIRE(Math::Magnitude(StateOf(test, character).carriedVelocity) < 1.0e-6f);
    (void)lift;
}

TEST_CASE("A character pushes a box it walks into", "[character-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(2.0f, 0.5f, 0.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    test.Run(3.0);
    INFO("box at x " << PositionOf(box).x << ", character at " << PositionOf(character).x);
    REQUIRE(PositionOf(box).x > 4.0f);
    REQUIRE(PositionOf(box).x > PositionOf(character).x);
}

TEST_CASE("Two runs of the same characters are bit-identical", "[character-spike]")
{
    const auto run = []() {
        PhysicsApp test;
        AddGround(test.Entities());
        AddLift(test, Math::Vec3(3.0f, 0.5f, 0.0f), Math::Vec3(0.0f, 1.0f, 1.0f), 1.0);
        for (int i = 0; i < 4; ++i)
        {
            AddBody(test.Entities(),
                    Math::Vec3(1.0f, 0.5f + 1.1f * static_cast<float>(i), 0.3f * static_cast<float>(i)));
        }
        std::vector<Entity> characters;
        std::vector<std::shared_ptr<Input>> inputs;
        for (int i = 0; i < 3; ++i)
        {
            characters.push_back(AddCharacter(test.Entities(), Math::Vec3(-1.0f, 0.0f, 0.8f * static_cast<float>(i))));
            inputs.push_back(Drive(test.app, characters.back()));
            // Into each other and the boxes, then onto the lift.
            inputs.back()->walk = Math::Vec3(2.0f, 0.0f, 0.5f - 0.5f * static_cast<float>(i));
        }
        std::vector<Transform> poses;
        for (int t = 0; t < 300; ++t)
        {
            if (t % 50 == 0)
            {
                inputs[static_cast<std::size_t>(t / 50) % inputs.size()]->jump = true;
            }
            test.Run(PhysicsTest::tick);
            for (const Entity& character : characters)
            {
                poses.push_back(GetWorldTransform(character));
            }
        }
        return poses;
    };
    const std::vector<Transform> first = run();
    const std::vector<Transform> second = run();
    REQUIRE(first.size() == second.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        differing += std::memcmp(&first[i].pos, &second[i].pos, sizeof(Math::Vec3)) == 0 ? 0 : 1;
    }
    INFO(differing << " of " << first.size() << " poses differ");
    REQUIRE(differing == 0);
}

TEST_CASE("Between ticks a character is shown between its last two step poses, and a teleport snaps",
          "[character-spike]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(3.0f, 0.0f, 0.0f);
    // Game code turns it a little each tick.
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[character](App&) {
                                      Transform& pose = character.GetComponent<Transform>();
                                      pose.rot = Math::Normalized(pose.rot *
                                                                  Math::AngleAxis(0.05f, Math::Vec3(0.0f, 1.0f, 0.0f)));
                                  }});
    auto steps = std::make_shared<std::vector<Transform>>();
    test.app.AddSequentialSystems(SchedulePriority::PostTick,
                                  {[steps, character](App&) { steps->push_back(GetWorldTransform(character)); }});
    test.Run(0.5);

    int blended = 0;
    for (int frame = 0; frame < 30; ++frame)
    {
        const std::size_t before = steps->size();
        test.app.RunFrame(PhysicsTest::tick * 0.3);
        if (steps->size() < 2 || steps->size() == before)
        {
            continue;
        }
        const float fraction = static_cast<float>(test.app.GetTime().accumulator / test.app.GetTime().tickRate);
        const Transform& previous = (*steps)[steps->size() - 2];
        const Transform& current = steps->back();
        const Math::Vec3 expected = previous.pos + (current.pos - previous.pos) * fraction;
        const Transform shown = GetWorldTransform(character);
        INFO("frame " << frame << ": shown x " << shown.pos.x << ", expected " << expected.x);
        REQUIRE(Math::Magnitude(shown.pos - expected) < 1.0e-4f);
        REQUIRE(std::abs(glm::dot(shown.rot, Math::Slerp(previous.rot, current.rot, fraction))) > 0.99999f);
        ++blended;
    }
    REQUIRE(blended > 5);

    // Teleported in Update: shown where it was put, and its next step pose does not blend from where it was.
    character.GetComponent<Transform>().pos = Math::Vec3(-5.0f, footToCentre, 5.0f);
    for (int frame = 0; frame < 6; ++frame)
    {
        test.app.RunFrame(PhysicsTest::tick * 0.3);
        INFO("frame " << frame << " after the teleport: x " << PositionOf(character).x);
        REQUIRE(PositionOf(character).x < -4.5f);
    }
}
