// Contact and trigger events through the frame seam, read as game code reads them: through EventReader<T>.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Events.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    template <typename TEvent>
    struct Recorded
    {
        EventReader<TEvent> reader;
        std::vector<TEvent> events;

        void Collect(const App& app)
        {
            for (const TEvent& event : reader.Read(app))
            {
                events.push_back(event);
            }
        }
    };

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    /** Every physics event sent while it runs frames, read after each frame as a system would. */
    struct EventLog
    {
        Recorded<ContactBegin> begins;
        Recorded<ContactEnd> ends;
        Recorded<TriggerEnter> enters;
        Recorded<TriggerExit> exits;

        void Run(PhysicsApp& test, double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                begins.Collect(test.app);
                ends.Collect(test.app);
                enters.Collect(test.app);
                exits.Collect(test.app);
            }
        }
    };
} // namespace

TEST_CASE("A box with ContactEvents landing on the ground sends one ContactBegin", "[physics][events]")
{
    PhysicsApp test;
    EventLog log;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    box.AddComponent<ContactEvents>();

    log.Run(test, 2.0);

    INFO("the box lands once and stays, so one contact begins, got " << log.begins.events.size());
    REQUIRE(log.begins.events.size() == 1);
    const ContactBegin& begin = log.begins.events[0];
    INFO("A is the ground (the lower entity id) and B the box, as body and as collider");
    REQUIRE(begin.bodyA == ground);
    REQUIRE(begin.bodyB == box);
    REQUIRE(begin.colliderA == ground);
    REQUIRE(begin.colliderB == box);
    INFO("the normal points from the ground up to the box, it is (" << begin.normal.x << ", " << begin.normal.y << ", "
                                                                    << begin.normal.z << ")");
    REQUIRE(begin.normal.y == Approx(1.0f).margin(1e-3));
    INFO("the box falls 1.5 m before it touches, closing at about sqrt(2 x 9.81 x 1.5) = 5.4 m/s, got "
         << begin.relativeSpeed);
    REQUIRE(begin.relativeSpeed == Approx(5.4f).margin(0.5f));
    INFO("it touches on the ground's top face (y = 0) under the box, at y = " << begin.point.y);
    REQUIRE(begin.point.y == Approx(0.0f).margin(0.05f));
    REQUIRE(std::abs(begin.point.x) <= 0.5f + 1e-3f);
    INFO("nothing ended while the box rests");
    REQUIRE(log.ends.events.empty());
}

TEST_CASE("Lifting a resting box off the ground sends one ContactEnd naming the same pair", "[physics][events]")
{
    PhysicsApp test;
    EventLog log;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    box.AddComponent<ContactEvents>();
    log.Run(test, 2.0);

    // Game code lifts it well clear and lets it hang there.
    box.GetComponent<RigidBody>().gravityScale = 0.0f;
    box.GetComponent<Transform>().pos.y = 3.0f;
    log.Run(test, 0.5);

    INFO("one contact began and one ended, got " << log.begins.events.size() << " and " << log.ends.events.size());
    REQUIRE(log.begins.events.size() == 1);
    REQUIRE(log.ends.events.size() == 1);
    const ContactEnd& end = log.ends.events[0];
    INFO("the end names the ground and the box, in the order its begin did");
    REQUIRE(end.bodyA == ground);
    REQUIRE(end.bodyB == box);
    REQUIRE(end.colliderA == ground);
    REQUIRE(end.colliderB == box);
}

TEST_CASE("Without ContactEvents on either body, a landing sends no contact events", "[physics][events]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));

    log.Run(test, 2.0);

    INFO("no body asked for contact events, yet " << log.begins.events.size() << " began");
    REQUIRE(log.begins.events.empty());
    REQUIRE(log.ends.events.empty());
}

TEST_CASE("ContactEvents on the ground reports a box landing on it", "[physics][events]")
{
    PhysicsApp test;
    EventLog log;
    Entity ground = AddGround(test.Entities());
    ground.AddComponent<ContactEvents>();
    AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));

    log.Run(test, 2.0);

    INFO("either body asking is enough, got " << log.begins.events.size() << " begins");
    REQUIRE(log.begins.events.size() == 1);
}

TEST_CASE("A box resting on the ground that falls asleep does not end its contact", "[physics][events][sleep]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    box.AddComponent<ContactEvents>();

    log.Run(test, 4.0);

    INFO("after resting for seconds the box is asleep");
    REQUIRE(World(test).IsSleeping(box));
    INFO("its contact began once and never ended, got " << log.begins.events.size() << " begins and "
                                                        << log.ends.events.size() << " ends");
    REQUIRE(log.begins.events.size() == 1);
    REQUIRE(log.ends.events.empty());
    int contacts = 0;
    World(test).ForEachContact(box, [&](const Contact&) { ++contacts; });
    INFO("asleep, it is still touching the ground");
    REQUIRE(contacts == 1);
}

TEST_CASE("A sleeping box woken and lifted away ends its contact", "[physics][events][sleep]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    box.AddComponent<ContactEvents>();
    log.Run(test, 4.0);
    REQUIRE(World(test).IsSleeping(box));

    box.GetComponent<RigidBody>().gravityScale = 0.0f;
    box.GetComponent<Transform>().pos.y = 3.0f;
    log.Run(test, 0.25);

    INFO("a contact kept through sleep still ends when the bodies separate, got " << log.ends.events.size());
    REQUIRE(log.ends.events.size() == 1);
}

TEST_CASE("ForEachContact lists what a body touches now", "[physics][events][contacts]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);

    std::vector<Contact> contacts;
    World(test).ForEachContact(box, [&](const Contact& contact) { contacts.push_back(contact); });
    INFO("the box touches the ground only, whether or not contacts are reported as events");
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts[0].body == box);
    REQUIRE(contacts[0].collider == box);
    REQUIRE(contacts[0].otherBody == ground);
    REQUIRE(contacts[0].otherCollider == ground);
    INFO("the normal points from the box down to the ground, it is " << contacts[0].normal.y);
    REQUIRE(contacts[0].normal.y == Approx(-1.0f).margin(1e-3));

    int groundContacts = 0;
    World(test).ForEachContact(ground, [&](const Contact& contact) {
        ++groundContacts;
        INFO("asked of the ground, the other body is the box");
        REQUIRE(contact.otherBody == box);
    });
    REQUIRE(groundContacts == 1);

    box.GetComponent<RigidBody>().gravityScale = 0.0f;
    box.GetComponent<Transform>().pos.y = 3.0f;
    test.Run(0.1);
    int after = 0;
    World(test).ForEachContact(box, [&](const Contact&) { ++after; });
    INFO("lifted clear, it touches nothing");
    REQUIRE(after == 0);
}

TEST_CASE("A ball falling through a trigger enters and exits it once", "[physics][events][triggers]")
{
    PhysicsApp test;
    EventLog log;
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    zone.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(1.0f, 0.5f, 1.0f)}});
    zone.AddComponent<Trigger>();
    Entity ball = AddBody(test.Entities(), Math::Vec3(0.0f, 6.0f, 0.0f), SphereShape{0.25f});

    log.Run(test, 1.5);

    INFO("the ball passes through the zone: one enter and one exit, got " << log.enters.events.size() << " and "
                                                                          << log.exits.events.size());
    REQUIRE(log.enters.events.size() == 1);
    REQUIRE(log.exits.events.size() == 1);
    INFO("each names the trigger, and the ball as body and collider");
    REQUIRE(log.enters.events[0].trigger == zone);
    REQUIRE(log.enters.events[0].otherBody == ball);
    REQUIRE(log.enters.events[0].otherCollider == ball);
    REQUIRE(log.exits.events[0].trigger == zone);
    REQUIRE(log.exits.events[0].otherBody == ball);
    INFO("a trigger pushes nothing: the ball fell straight through, well below the zone now");
    REQUIRE(PositionOf(ball).y < 1.0f);
    INFO("a trigger overlap is not a contact");
    REQUIRE(log.begins.events.empty());
}

TEST_CASE("A trigger under a moving body follows it and uses its own filter", "[physics][events][triggers]")
{
    PhysicsApp test;
    EventLog log;
    constexpr std::uint32_t pickups = 1u << 1;
    constexpr std::uint32_t radius = 1u << 2;

    Entity player = AddBody(test.Entities(), Math::Vec3(-4.0f, 0.5f, 0.0f), BoxShape{}, MotionType::Kinematic);
    Entity sensor = Place(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    player.AddChild(sensor);
    sensor.AddComponent<Collider>(Collider{SphereShape{0.5f}});
    sensor.AddComponent<Trigger>();
    sensor.AddComponent<CollisionFilter>(CollisionFilter{.layers = radius, .mask = pickups});

    // Two static boxes on the sensor's path, 3.5 up; only one is in the layer the sensor looks for.
    Entity pickup = Place(test.Entities(), Math::Vec3(-1.0f, 3.5f, 0.0f), Math::Vec3(0.5f));
    pickup.AddComponent<Collider>();
    pickup.AddComponent<CollisionFilter>(CollisionFilter{.layers = pickups, .mask = ~0u});
    Entity scenery = Place(test.Entities(), Math::Vec3(1.0f, 3.5f, 0.0f), Math::Vec3(0.5f));
    scenery.AddComponent<Collider>();

    // Game code walks the player 8 m along x over two seconds.
    int ticks = 0;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                      ++ticks;
                                      player.GetComponent<Transform>().pos.x =
                                          -4.0f + 8.0f * std::min(1.0f, static_cast<float>(ticks) / 120.0f);
                                  }});
    log.Run(test, 2.5);

    INFO("the sensor, carried over both boxes, enters only the pickup: got " << log.enters.events.size() << " enters");
    REQUIRE(log.enters.events.size() == 1);
    REQUIRE(log.enters.events[0].trigger == sensor);
    REQUIRE(log.enters.events[0].otherCollider == pickup);
    INFO("and leaves it once it has passed");
    REQUIRE(log.exits.events.size() == 1);
    REQUIRE(log.exits.events[0].otherCollider == pickup);
}

TEST_CASE("A box that falls asleep inside a trigger does not exit it", "[physics][events][triggers][sleep]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    zone.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(2.0f, 1.4f, 2.0f)}});
    zone.AddComponent<Trigger>();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));

    log.Run(test, 4.0);

    INFO("the box came to rest in the zone and fell asleep");
    REQUIRE(World(test).IsSleeping(box));
    INFO("it entered once and never exited, got " << log.enters.events.size() << " enters and "
                                                  << log.exits.events.size() << " exits");
    REQUIRE(log.enters.events.size() == 1);
    REQUIRE(log.exits.events.empty());
}

TEST_CASE("Destroying a body in contact sends ContactEnd on the next step, naming it", "[physics][events][removal]")
{
    PhysicsApp test;
    EventLog log;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    ground.AddComponent<ContactEvents>();
    log.Run(test, 1.0);
    REQUIRE(log.begins.events.size() == 1);
    const std::uint32_t boxId = box.GetId();

    test.Entities().DestroyEntity(box);
    log.Run(test, tick);

    INFO("the contact ends on the very next step, got " << log.ends.events.size() << " ends");
    REQUIRE(log.ends.events.size() == 1);
    INFO("it names the destroyed box, which no longer exists");
    REQUIRE(log.ends.events[0].bodyB.GetId() == boxId);
    REQUIRE_FALSE(test.Entities().HasEntity(log.ends.events[0].bodyB.GetId()));
}

TEST_CASE("Removing the Collider of a body in contact sends ContactEnd on the next step", "[physics][events][removal]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    box.AddComponent<ContactEvents>();
    log.Run(test, 1.0);

    box.RemoveComponent<Collider>();
    log.Run(test, tick);

    INFO("one end, got " << log.ends.events.size());
    REQUIRE(log.ends.events.size() == 1);
    REQUIRE(log.ends.events[0].bodyB == box);
}

TEST_CASE("Disabling ContactEvents ends the contacts it reported", "[physics][events][removal]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    box.AddComponent<ContactEvents>();
    log.Run(test, 1.0);

    box.GetComponent<ContactEvents>().enabled = false;
    log.Run(test, tick);

    INFO("every begin gets its end: switching reporting off ends the contact, got " << log.ends.events.size());
    REQUIRE(log.ends.events.size() == 1);
}

TEST_CASE("Disabling a trigger exits what is inside on the next step; enabling it enters again",
          "[physics][events][triggers][removal]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    zone.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(2.0f, 1.4f, 2.0f)}});
    zone.AddComponent<Trigger>();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    log.Run(test, 1.0);
    REQUIRE(log.enters.events.size() == 1);

    zone.GetComponent<Trigger>().enabled = false;
    log.Run(test, tick);
    INFO("disabled, the zone reports the box leaving at once, got " << log.exits.events.size() << " exits");
    REQUIRE(log.exits.events.size() == 1);
    REQUIRE(log.exits.events[0].otherBody == box);

    log.Run(test, 0.5);
    INFO("while disabled it reports nothing more");
    REQUIRE(log.enters.events.size() == 1);

    zone.GetComponent<Trigger>().enabled = true;
    log.Run(test, 0.1);
    INFO("enabled again, the box inside enters again, got " << log.enters.events.size() << " enters in all");
    REQUIRE(log.enters.events.size() == 2);
}

TEST_CASE("Destroying a trigger sends TriggerExit on the next step", "[physics][events][triggers][removal]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    zone.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(2.0f, 1.4f, 2.0f)}});
    zone.AddComponent<Trigger>();
    AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    log.Run(test, 1.0);
    const std::uint32_t zoneId = zone.GetId();

    test.Entities().DestroyEntity(zone);
    log.Run(test, tick);

    INFO("one exit naming the destroyed trigger, got " << log.exits.events.size());
    REQUIRE(log.exits.events.size() == 1);
    REQUIRE(log.exits.events[0].trigger.GetId() == zoneId);
}

TEST_CASE("Contact events come in the same order on every run", "[physics][events][determinism]")
{
    // Boxes landing in a grid, every one reporting; the order game code reads must not depend on worker threads.
    const auto run = [] {
        PhysicsApp test;
        EventLog log;
        AddGround(test.Entities());
        for (int x = 0; x < 6; ++x)
        {
            for (int z = 0; z < 6; ++z)
            {
                Entity box = AddBody(test.Entities(), Math::Vec3(1.5f * static_cast<float>(x - 3),
                                                                 1.0f + 0.1f * static_cast<float>(x + z),
                                                                 1.5f * static_cast<float>(z - 3)));
                box.AddComponent<ContactEvents>();
            }
        }
        log.Run(test, 1.5);
        return log.begins.events;
    };

    const std::vector<ContactBegin> first = run();
    const std::vector<ContactBegin> second = run();

    INFO("every box lands once: 36 begins, got " << first.size());
    REQUIRE(first.size() == 36);
    REQUIRE(second.size() == first.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        INFO("begin " << i << " is the same pair, at the same point, on both runs");
        REQUIRE(first[i].bodyA.GetId() == second[i].bodyA.GetId());
        REQUIRE(first[i].bodyB.GetId() == second[i].bodyB.GetId());
        REQUIRE(first[i].point == second[i].point);
    }
}

TEST_CASE("A ray through a trigger misses it unless the filter asks for triggers", "[physics][triggers][queries]")
{
    PhysicsApp test;
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    zone.AddComponent<Collider>();
    zone.AddComponent<Trigger>();
    test.Run(tick);

    const Math::Vec3 from(-5.0f, 0.0f, 0.0f);
    const Math::Vec3 along(1.0f, 0.0f, 0.0f);
    INFO("by default a ray passes through a trigger");
    REQUIRE_FALSE(World(test).Raycast(from, along, 10.0f).has_value());
    const auto hit = World(test).Raycast(from, along, 10.0f, QueryFilter{.hitTriggers = true});
    INFO("asked for triggers, it hits the zone's near face, 4.5 m away");
    REQUIRE(hit.has_value());
    REQUIRE(hit->collider == zone);
    REQUIRE(hit->distance == Approx(4.5f).margin(1e-3));
}
