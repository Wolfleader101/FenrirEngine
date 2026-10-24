// A character's contacts as game code sees a body's: ContactBegin and ContactEnd naming its entity, from its moves and,
// with dynamic bodies, its inner body's step, in one sorted stream; ForEachContact listing its end-of-move contacts;
// and characters bit-identical across runs and thread counts.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    PhysicsWorld& WorldOf(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    // Every ContactBegin and ContactEnd sent while it runs frames, and how many each frame sent.
    struct ContactLog
    {
        EventReader<ContactBegin> beginReader;
        EventReader<ContactEnd> endReader;
        std::vector<ContactBegin> begins;
        std::vector<ContactEnd> ends;
        // Per frame: how many begins, then how many ends, it sent.
        std::vector<std::pair<std::size_t, std::size_t>> perFrame;

        void Run(PhysicsApp& test, double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                std::size_t beganNow = 0;
                std::size_t endedNow = 0;
                for (const ContactBegin& event : beginReader.Read(test.app))
                {
                    begins.push_back(event);
                    ++beganNow;
                }
                for (const ContactEnd& event : endReader.Read(test.app))
                {
                    ends.push_back(event);
                    ++endedNow;
                }
                perFrame.emplace_back(beganNow, endedNow);
            }
        }

        [[nodiscard]] std::size_t BeginsBetween(const Entity& a, const Entity& b) const
        {
            std::size_t count = 0;
            for (const ContactBegin& event : begins)
            {
                count += (event.bodyA == a && event.bodyB == b) || (event.bodyA == b && event.bodyB == a) ? 1 : 0;
            }
            return count;
        }

        [[nodiscard]] std::size_t EndsBetween(const Entity& a, const Entity& b) const
        {
            std::size_t count = 0;
            for (const ContactEnd& event : ends)
            {
                count += (event.bodyA == a && event.bodyB == b) || (event.bodyA == b && event.bodyB == a) ? 1 : 0;
            }
            return count;
        }
    };

    // A static floor slab 1 m thick, its top face at y = 0, from x = `fromX` to `toX`, 4 m deep.
    Entity AddFloor(EntityList& entities, float fromX, float toX)
    {
        return AddStatic(entities, Math::Vec3(0.5f * (fromX + toX), -0.5f, 0.0f),
                         Math::Vec3(0.5f * (toX - fromX), 0.5f, 2.0f));
    }
} // namespace

TEST_CASE("A character walking onto and off a floor with ContactEvents begins and ends one contact with it",
          "[physics][characters][events]")
{
    PhysicsApp test;
    AddFloor(test.Entities(), -5.0f, 0.0f);
    Entity floor = AddFloor(test.Entities(), 0.0f, 2.0f);
    AddFloor(test.Entities(), 2.0f, 7.0f);
    floor.AddComponent<ContactEvents>();
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-1.5f, 0.0f, 0.0f));
    Drive(test.app, character)->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    ContactLog log;

    log.Run(test, 5.0);

    INFO("from x = -1.5 at 1 m/s for 5 s it walked over the middle floor to x = " << PositionOf(character).x);
    REQUIRE(PositionOf(character).x == Approx(3.5f).margin(0.05f));
    INFO("one begin and one end, got " << log.begins.size() << " and " << log.ends.size());
    REQUIRE(log.begins.size() == 1);
    REQUIRE(log.ends.size() == 1);
    const ContactBegin& begin = log.begins[0];
    INFO("A is the floor (the lower entity id), B the character's entity as body and collider");
    REQUIRE(begin.bodyA == floor);
    REQUIRE(begin.colliderA == floor);
    REQUIRE(begin.bodyB == character);
    REQUIRE(begin.colliderB == character);
    INFO("the normal points from the floor up to the character, which meets its rounded edge first: ("
         << begin.normal.x << ", " << begin.normal.y << ", " << begin.normal.z << ")");
    REQUIRE(begin.normal.y > 0.95f);
    INFO("it touches on the floor's top face, at y = " << begin.point.y << ", x = " << begin.point.x);
    REQUIRE(begin.point.y == Approx(0.0f).margin(0.05f));
    REQUIRE(begin.point.x >= -0.35f);
    REQUIRE(begin.point.x <= 0.35f);
    const ContactEnd& end = log.ends[0];
    INFO("the end names the same pair in the same order");
    REQUIRE(end.bodyA == floor);
    REQUIRE(end.colliderA == floor);
    REQUIRE(end.bodyB == character);
    REQUIRE(end.colliderB == character);
}

TEST_CASE("A character landing on a floor begins the contact with its closing speed at the start of the move",
          "[physics][characters][events]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 1.25f, 0.0f));
    character.AddComponent<ContactEvents>();
    Drive(test.app, character);
    ContactLog log;

    log.Run(test, 1.0);

    INFO("it lands once and stands: one begin, no end, got " << log.begins.size() << " and " << log.ends.size());
    REQUIRE(log.begins.size() == 1);
    REQUIRE(log.ends.empty());
    REQUIRE(log.begins[0].bodyA == floor);
    REQUIRE(log.begins[0].bodyB == character);
    INFO("falling 1.25 m it closes at about sqrt(2 x 9.81 x 1.25) = 4.95 m/s, less up to a tick of gravity, got "
         << log.begins[0].relativeSpeed);
    REQUIRE(log.begins[0].relativeSpeed == Approx(4.95f).margin(0.3f));
}

TEST_CASE("A ball bouncing off a standing character begins one contact naming the character, then ends it",
          "[physics][characters][events]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    test.Run(0.25);

    Entity ball = Place(test.Entities(), Math::Vec3(-3.0f, footToCentre, 0.0f));
    ball.AddComponent<RigidBody>(
        RigidBody{.mass = 1.0f, .gravityScale = 0.0f, .linearVelocity = Math::Vec3(10.0f, 0.0f, 0.0f)});
    ball.AddComponent<Collider>(Collider{SphereShape{.radius = 0.15f}});
    ball.AddComponent<PhysicsMaterial>(PhysicsMaterial{.restitution = 0.8f});
    ball.AddComponent<ContactEvents>();
    ContactLog log;

    log.Run(test, 1.0);

    INFO("the ball came back off the character, at " << ball.GetComponent<RigidBody>().linearVelocity.x << " m/s");
    REQUIRE(ball.GetComponent<RigidBody>().linearVelocity.x < -1.0f);
    INFO("one begin and one end, got " << log.begins.size() << " and " << log.ends.size());
    REQUIRE(log.begins.size() == 1);
    REQUIRE(log.ends.size() == 1);
    INFO("A is the character (the lower entity id), named as body and collider, and B the ball");
    REQUIRE(log.begins[0].bodyA == character);
    REQUIRE(log.begins[0].colliderA == character);
    REQUIRE(log.begins[0].bodyB == ball);
    REQUIRE(log.begins[0].colliderB == ball);
    INFO("the normal points from the character to the ball, back along its flight: x = " << log.begins[0].normal.x);
    REQUIRE(log.begins[0].normal.x == Approx(-1.0f).margin(0.05f));
    INFO("the ball closed at about 10 m/s, got " << log.begins[0].relativeSpeed);
    REQUIRE(log.begins[0].relativeSpeed == Approx(10.0f).margin(1.0f));
    REQUIRE(log.ends[0].bodyA == character);
    REQUIRE(log.ends[0].bodyB == ball);
    INFO("nothing about the floor: neither it nor the character has ContactEvents");
    REQUIRE(log.BeginsBetween(floor, character) == 0);
}

TEST_CASE("Character and body contacts arrive in one stream sorted by entity, one pair per entity pair",
          "[physics][characters][events]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    // Entity ids interleave characters and boxes, so the sort must mix the two kinds of contact.
    Entity first = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Entity box = AddBody(test.Entities(), Math::Vec3(3.0f, 0.5f, 0.0f));
    Entity second = AddCharacter(test.Entities(), Math::Vec3(0.6f, 0.0f, 0.0f));
    Entity other = AddBody(test.Entities(), Math::Vec3(-3.0f, 0.5f, 0.0f));
    for (Entity each : {first, box, second, other})
    {
        each.AddComponent<ContactEvents>();
    }
    Drive(test.app, first);
    Drive(test.app, second)->walk = Math::Vec3(-1.0f, 0.0f, 0.0f);
    ContactLog log;

    log.Run(test, 1.0);

    INFO("each of the four began one contact with the ground");
    REQUIRE(log.BeginsBetween(ground, first) == 1);
    REQUIRE(log.BeginsBetween(ground, box) == 1);
    REQUIRE(log.BeginsBetween(ground, second) == 1);
    REQUIRE(log.BeginsBetween(ground, other) == 1);
    INFO("the second character walking into the first is one pair, met by both of their moves, got "
         << log.BeginsBetween(first, second));
    REQUIRE(log.BeginsBetween(first, second) == 1);
    REQUIRE(log.EndsBetween(first, second) == 0);
    INFO("nothing else touched");
    REQUIRE(log.begins.size() == 5);
    REQUIRE(log.ends.empty());

    const auto key = [](const ContactBegin& event) {
        return std::make_tuple(event.bodyA.GetId(), event.bodyB.GetId(), event.colliderA.GetId(),
                               event.colliderB.GetId());
    };
    std::size_t at = 0;
    for (const auto& [began, ended] : log.perFrame)
    {
        for (std::size_t i = at + 1; i < at + began; ++i)
        {
            INFO("a frame's begins come sorted by (body A, body B, collider A, collider B): begin " << i);
            REQUIRE(key(log.begins[i - 1]) < key(log.begins[i]));
        }
        at += began;
    }
    for (const ContactBegin& event : log.begins)
    {
        INFO("A is always the lower entity id");
        REQUIRE(event.bodyA.GetId() < event.bodyB.GetId());
    }
    INFO("the first frame's begins mix characters and boxes in one sort: got " << log.perFrame[0].first);
    REQUIRE(log.perFrame[0].first >= 4);
}

TEST_CASE("A character that stops being one ends each contact it reported on the next step",
          "[physics][characters][events][removal]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    floor.AddComponent<ContactEvents>();
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    ContactLog log;
    log.Run(test, 0.5);
    REQUIRE(log.begins.size() == 1);
    const std::uint32_t characterId = character.GetId();

    const auto how = GENERATE(as<std::string>{}, "disabled", "made inactive", "losing its CharacterBody", "destroyed",
                              "its floor dropping ContactEvents");
    {
        if (how == "disabled")
        {
            character.GetComponent<CharacterBody>().enabled = false;
        }
        else if (how == "made inactive")
        {
            character.RemoveComponent<Collider>();
        }
        else if (how == "losing its CharacterBody")
        {
            character.RemoveComponent<CharacterBody>();
        }
        else if (how == "destroyed")
        {
            test.Entities().DestroyEntity(character);
        }
        else
        {
            floor.GetComponent<ContactEvents>().enabled = false;
        }
        log.Run(test, tick);

        INFO(how << ": the contact ends on the very next step, got " << log.ends.size() << " ends");
        REQUIRE(log.ends.size() == 1);
        REQUIRE(log.ends[0].bodyA == floor);
        REQUIRE(log.ends[0].bodyB.GetId() == characterId);
        REQUIRE(log.ends[0].colliderB.GetId() == characterId);
        log.Run(test, 0.5);
        INFO("and nothing begins again");
        REQUIRE(log.begins.size() == 1);
        REQUIRE(log.ends.size() == 1);
    }
}

TEST_CASE("ForEachContact lists the point and normal of a wall a character walks into", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // Its face towards the character at x = 1.
    Entity wall = AddStatic(test.Entities(), Math::Vec3(1.5f, 1.0f, 0.0f), Math::Vec3(0.5f, 1.0f, 2.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character)->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    test.Run(1.0);

    std::vector<Contact> withWall;
    WorldOf(test).ForEachContact(character, [&](const Contact& contact) {
        if (contact.otherBody == wall)
        {
            withWall.push_back(contact);
        }
    });

    INFO("one contact with the wall, got " << withWall.size());
    REQUIRE(withWall.size() == 1);
    const Contact& contact = withWall[0];
    INFO("it names the character as body and collider, and the wall as the other");
    REQUIRE(contact.body == character);
    REQUIRE(contact.collider == character);
    REQUIRE(contact.otherCollider == wall);
    INFO("on the wall's face at x = 1, between the feet and the head: (" << contact.point.x << ", " << contact.point.y
                                                                         << ")");
    REQUIRE(contact.point.x == Approx(1.0f).margin(0.05f));
    REQUIRE(contact.point.y > 0.0f);
    REQUIRE(contact.point.y < 2.0f * footToCentre);
    INFO("the normal points from the character into the wall: (" << contact.normal.x << ", " << contact.normal.y << ", "
                                                                 << contact.normal.z << ")");
    REQUIRE(contact.normal.x == Approx(1.0f).margin(1.0e-3));

    std::vector<Contact> fromWall;
    WorldOf(test).ForEachContact(wall, [&](const Contact& each) { fromWall.push_back(each); });
    INFO("asked of the wall, the same contact names the character, its normal turned: " << fromWall.size());
    REQUIRE(fromWall.size() == 1);
    REQUIRE(fromWall[0].otherBody == character);
    REQUIRE(fromWall[0].normal.x == Approx(-1.0f).margin(1.0e-3));
}

namespace
{
    // Three characters walking loops into each other and jumping, one riding a lift, among four boxes: where each
    // character and box was, and how fast, each tick, and every contact event, as bits.
    std::vector<std::byte> BumpingCharacters(const PhysicsConfig& config)
    {
        PhysicsApp test(config);
        AddGround(test.Entities());
        Entity lift = AddBody(test.Entities(), Math::Vec3(3.0f, -0.1f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.1f, 1.0f)},
                              MotionType::Kinematic);
        std::vector<Entity> boxes;
        for (int i = 0; i < 4; ++i)
        {
            Entity box =
                AddBody(test.Entities(),
                        Math::Vec3(-1.0f + 0.7f * static_cast<float>(i), 0.25f, 0.8f - 0.4f * static_cast<float>(i)),
                        BoxShape{Math::Vec3(0.25f)});
            box.AddComponent<ContactEvents>();
            boxes.push_back(box);
        }
        std::vector<Entity> characters;
        std::vector<std::shared_ptr<Input>> inputs;
        const Math::Vec3 starts[] = {Math::Vec3(-2.0f, 0.0f, 0.0f), Math::Vec3(2.0f, 0.0f, 0.3f),
                                     Math::Vec3(3.0f, 0.0f, 0.0f)};
        for (const Math::Vec3& start : starts)
        {
            Entity character = AddCharacter(test.Entities(), start);
            character.AddComponent<ContactEvents>();
            characters.push_back(character);
            inputs.push_back(Drive(test.app, character));
        }
        auto ticks = std::make_shared<int>(0);
        test.app.AddSequentialSystems(SchedulePriority::Tick, {[ticks, lift](App&) {
                                          const float speed = (*ticks / 60) % 2 == 0 ? 1.0f : -1.0f;
                                          lift.GetComponent<Transform>().pos.y += speed * static_cast<float>(tick);
                                          ++*ticks;
                                      }});
        ContactLog log;

        std::vector<std::byte> bits;
        const auto record = [&bits](const auto& value) {
            const std::span<const std::byte> bytes = std::as_bytes(std::span(&value, 1));
            bits.insert(bits.end(), bytes.begin(), bytes.end());
        };
        for (int t = 0; t < 300; ++t)
        {
            // Towards each other and back, so they bump, with jumps at different ticks.
            inputs[0]->walk = Math::Vec3((t / 75) % 2 == 0 ? 2.0f : -2.0f, 0.0f, 0.2f);
            inputs[1]->walk = Math::Vec3((t / 75) % 2 == 0 ? -2.0f : 2.0f, 0.0f, -0.2f);
            inputs[2]->walk = Math::Vec3(0.0f, 0.0f, (t / 50) % 2 == 0 ? 0.5f : -0.5f);
            inputs[0]->jump = t % 90 == 30;
            inputs[1]->jump = t % 70 == 50;
            inputs[2]->jump = t % 110 == 80;
            const std::size_t begun = log.begins.size();
            const std::size_t ended = log.ends.size();
            log.Run(test, tick);
            for (const Entity& each : characters)
            {
                record(PositionOf(each));
                record(each.GetComponent<CharacterBody>().velocity);
                record(each.GetComponent<CharacterBody>().state.movedVelocity);
            }
            for (const Entity& each : boxes)
            {
                record(PositionOf(each));
                record(each.GetComponent<RigidBody>().linearVelocity);
            }
            for (std::size_t i = begun; i < log.begins.size(); ++i)
            {
                const ContactBegin& event = log.begins[i];
                record(std::make_tuple(event.bodyA.GetId(), event.bodyB.GetId(), event.colliderA.GetId(),
                                       event.colliderB.GetId()));
                record(event.point);
                record(event.normal);
                record(event.relativeSpeed);
            }
            for (std::size_t i = ended; i < log.ends.size(); ++i)
            {
                const ContactEnd& event = log.ends[i];
                record(std::make_tuple(event.bodyA.GetId(), event.bodyB.GetId(), event.colliderA.GetId(),
                                       event.colliderB.GetId()));
            }
        }
        return bits;
    }
} // namespace

TEST_CASE("Three characters bumping among boxes, with a lift and jumps, are bit-identical on every run and thread "
          "count, events included",
          "[physics][characters][events][determinism]")
{
    PhysicsConfig single = StepPosesOnly();
    single.useWorkerThreads = false;
    PhysicsConfig workers = StepPosesOnly();
    workers.useWorkerThreads = true;

    const std::vector<std::byte> first = BumpingCharacters(single);
    INFO("two runs on one thread agree, bit for bit");
    REQUIRE(first == BumpingCharacters(single));
    INFO("a run on the worker threads agrees with one on one thread");
    REQUIRE(first == BumpingCharacters(workers));
}
