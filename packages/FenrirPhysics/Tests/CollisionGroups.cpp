// Collision groups through the frame seam: bodies whose CollisionGroup holds the same non-zero group never touch,
// observed in where they come to rest, their contacts and their events as physics steps.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    /** Puts `body` in `group`. */
    void Group(Entity body, std::uint32_t group)
    {
        body.AddComponent<CollisionGroup>(CollisionGroup{group});
    }

    /** A unit box resting on the ground, and one dropped onto it from a metre above. */
    struct Stack
    {
        Entity lower;
        Entity upper;
    };

    Stack MakeStack(PhysicsApp& test)
    {
        AddGround(test.Entities());
        Stack stack;
        stack.lower = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
        stack.upper = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
        return stack;
    }

    // Where the centre of a unit box rests: on the ground, and on another resting there.
    constexpr float onGround = 0.5f;
    constexpr float onBox = 1.5f;

    /** Every contact and trigger event sent while it runs frames, read after each frame as a system would. */
    struct EventLog
    {
        EventReader<ContactBegin> beginReader;
        EventReader<ContactEnd> endReader;
        EventReader<TriggerEnter> enterReader;
        EventReader<TriggerExit> exitReader;
        std::vector<ContactBegin> begins;
        std::vector<ContactEnd> ends;
        std::vector<TriggerEnter> enters;
        std::vector<TriggerExit> exits;

        void Run(PhysicsApp& test, int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                for (const ContactBegin& event : beginReader.Read(test.app))
                {
                    begins.push_back(event);
                }
                for (const ContactEnd& event : endReader.Read(test.app))
                {
                    ends.push_back(event);
                }
                for (const TriggerEnter& event : enterReader.Read(test.app))
                {
                    enters.push_back(event);
                }
                for (const TriggerExit& event : exitReader.Read(test.app))
                {
                    exits.push_back(event);
                }
            }
        }
    };

    /** Whether the event names `a` and `b` as its two bodies, in either order. */
    template <typename TEvent>
    bool Names(const TEvent& event, const Entity& a, const Entity& b)
    {
        return (event.bodyA == a && event.bodyB == b) || (event.bodyA == b && event.bodyB == a);
    }

    template <typename TEvent>
    std::size_t CountNaming(const std::vector<TEvent>& events, const Entity& a, const Entity& b)
    {
        std::size_t count = 0;
        for (const TEvent& event : events)
        {
            count += Names(event, a, b) ? 1 : 0;
        }
        return count;
    }

    /** How many of the trigger events name `body` as the other body. */
    template <typename TEvent>
    std::size_t CountOf(const std::vector<TEvent>& events, const Entity& body)
    {
        std::size_t count = 0;
        for (const TEvent& event : events)
        {
            count += event.otherBody == body ? 1 : 0;
        }
        return count;
    }

    Entity AddTrigger(EntityList& entities, const Math::Vec3& centre, const Math::Vec3& halfExtents)
    {
        Entity zone = Place(entities, centre);
        zone.AddComponent<Collider>(Collider{BoxShape{halfExtents}});
        zone.AddComponent<Trigger>();
        return zone;
    }

    /** A static unit box at `centre`. */
    Entity AddPost(EntityList& entities, const Math::Vec3& centre)
    {
        Entity post = Place(entities, centre);
        post.AddComponent<Collider>();
        return post;
    }

    /** A joint of `kind` on a new joint entity between `bodyA` and `bodyB`, its rest pose at `anchor` where the bodies
     * are now, which keeps them from colliding. */
    Entity Join(PhysicsApp& test, const Entity& bodyA, const Entity& bodyB, const Math::Vec3& anchor,
                JointKind kind = FixedJoint{})
    {
        Entity joint = test.Entities().CreateEntity();
        const JointFrames frames = FrameFromWorld(bodyA, bodyB, JointFrame{anchor});
        joint.AddComponent<Joint>(
            Joint{.bodyA = bodyA, .bodyB = bodyB, .frameA = frames.frameA, .frameB = frames.frameB, .kind = kind});
        return joint;
    }
} // namespace

TEST_CASE("Two boxes in one collision group fall through each other", "[physics][groups]")
{
    PhysicsApp test;
    const Stack stack = MakeStack(test);
    Group(stack.lower, 7);
    Group(stack.upper, 7);

    test.Run(2.0);

    INFO("the upper box fell through the lower one to the ground: it is at y = " << PositionOf(stack.upper).y);
    REQUIRE(PositionOf(stack.upper).y == Approx(onGround).margin(restSlop));
    INFO("the lower box was not pushed: it is at y = " << PositionOf(stack.lower).y);
    REQUIRE(PositionOf(stack.lower).y == Approx(onGround).margin(restSlop));
    INFO("inside each other, the two are not in contact");
    REQUIRE_FALSE(Touching(test, stack.upper, stack.lower));
    REQUIRE_FALSE(Touching(test, stack.lower, stack.upper));
}

TEST_CASE("Boxes in different collision groups stack", "[physics][groups]")
{
    PhysicsApp test;
    const Stack stack = MakeStack(test);
    Group(stack.lower, 7);
    Group(stack.upper, 8);

    test.Run(2.0);

    INFO("the upper box landed on the lower one: it is at y = " << PositionOf(stack.upper).y);
    REQUIRE(PositionOf(stack.upper).y == Approx(onBox).margin(2.0f * restSlop));
    INFO("resting on it, the two are in contact");
    REQUIRE(Touching(test, stack.upper, stack.lower));
}

TEST_CASE("Group 0, and no CollisionGroup, is no collision group", "[physics][groups]")
{
    using Groups = std::pair<std::optional<std::uint32_t>, std::optional<std::uint32_t>>;
    const std::vector<Groups> cases = {
        {0u, 0u},
        {0u, std::nullopt},
        {7u, std::nullopt},
        {7u, 0u},
    };
    for (const auto& [lower, upper] : cases)
    {
        PhysicsApp test;
        const Stack stack = MakeStack(test);
        if (lower.has_value())
        {
            Group(stack.lower, *lower);
        }
        if (upper.has_value())
        {
            Group(stack.upper, *upper);
        }

        test.Run(2.0);

        INFO("lower box: " << (lower.has_value() ? "group " + std::to_string(*lower) : "no CollisionGroup")
                           << ", upper box: "
                           << (upper.has_value() ? "group " + std::to_string(*upper) : "no CollisionGroup"));
        INFO("the two are in no group together, so the upper box landed on the lower: it is at y = "
             << PositionOf(stack.upper).y);
        REQUIRE(PositionOf(stack.upper).y == Approx(onBox).margin(2.0f * restSlop));
    }
}

TEST_CASE("A collision group never adds contact the layers forbid", "[physics][groups][layers]")
{
    PhysicsApp test;
    const Stack stack = MakeStack(test);
    constexpr std::uint32_t lowerLayer = 1u << 1;
    Entity lower = stack.lower;
    Entity upper = stack.upper;
    lower.AddComponent<CollisionFilter>(CollisionFilter{.layers = lowerLayer, .mask = ~0u});
    upper.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u, .mask = ~lowerLayer});
    Group(lower, 7);
    Group(upper, 8);

    test.Run(2.0);

    INFO("the upper box's mask leaves out the lower box's layer, and being in different groups does not bring their "
         "contact back: it fell through to y = "
         << PositionOf(upper).y);
    REQUIRE(PositionOf(upper).y == Approx(onGround).margin(restSlop));
    INFO("inside each other, the two are not in contact");
    REQUIRE_FALSE(Touching(test, upper, lower));
}

TEST_CASE("A character's moves ignore the bodies of its collision group", "[physics][groups][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-3.0f, 0.0f, 0.0f));
    Group(character, 5);
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    // Two walls across its path: one in its group, and past it one in another.
    Group(AddStatic(test.Entities(), Math::Vec3(-1.0f, 1.0f, 0.0f), Math::Vec3(0.1f, 1.0f, 2.0f)), 5);
    Group(AddStatic(test.Entities(), Math::Vec3(1.5f, 1.0f, 0.0f), Math::Vec3(0.1f, 1.0f, 2.0f)), 6);

    test.Run(3.0);

    // The second wall's near face is at x = 1.4, and the capsule is 0.3 round.
    INFO("it walked through the wall of its own group and stopped at the wall of another: it is at x = "
         << PositionOf(character).x);
    REQUIRE(PositionOf(character).x == Approx(1.1f).margin(0.05f));
}

TEST_CASE("Bodies in a character's collision group fall through the character", "[physics][groups][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A character shaped as a box 1 m on each side, with a flat top to land on.
    Entity character = Place(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    character.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
    character.AddComponent<CharacterBody>();
    Group(character, 5);
    Drive(test.app, character);
    Entity mate = AddBody(test.Entities(), Math::Vec3(-0.2f, 3.0f, -0.2f), BoxShape{Math::Vec3(0.1f)});
    Group(mate, 5);
    Entity other = AddBody(test.Entities(), Math::Vec3(0.2f, 3.0f, 0.2f), BoxShape{Math::Vec3(0.1f)});
    Group(other, 6);

    test.Run(1.5);

    INFO("the box of the character's group fell through it to the ground: it is at y = " << PositionOf(mate).y);
    REQUIRE(PositionOf(mate).y == Approx(0.1f).margin(restSlop));
    INFO("the box of another group landed on its head: it is at y = " << PositionOf(other).y);
    REQUIRE(PositionOf(other).y == Approx(1.1f).margin(2.0f * restSlop));
    INFO("the character stands on the ground, not lifted by the box inside it: its centre is at y = "
         << PositionOf(character).y);
    REQUIRE(PositionOf(character).y == Approx(0.5f).margin(restSlop));
    INFO("the ground the character names is not the box of its group lying inside it");
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody != mate);
}

TEST_CASE("Changing a character's collision group takes effect on the next tick", "[physics][groups][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-3.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    Group(AddStatic(test.Entities(), Math::Vec3(-1.0f, 1.0f, 0.0f), Math::Vec3(0.1f, 1.0f, 2.0f)), 5);
    test.Run(1.5);
    // The wall's near face is at x = -1.1, and the capsule is 0.3 round.
    INFO("in no group, the character is stopped by the wall: it is at x = " << PositionOf(character).x);
    REQUIRE(PositionOf(character).x == Approx(-1.4f).margin(0.05f));

    Group(character, 5);
    const float before = PositionOf(character).x;
    test.app.RunFrame(tick);

    INFO("one tick after joining the wall's group it moved into the wall, by a tick's walk at 2 m/s: from x = "
         << before << " to " << PositionOf(character).x);
    REQUIRE(PositionOf(character).x - before == Approx(2.0f * static_cast<float>(tick)).margin(0.005f));
}

TEST_CASE("A trigger in a collision group does not fire for the bodies of its group", "[physics][groups][triggers]")
{
    PhysicsApp test;
    EventLog log;
    Entity zone = AddTrigger(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f), Math::Vec3(1.0f, 0.5f, 1.0f));
    Group(zone, 4);
    Entity mate = AddBody(test.Entities(), Math::Vec3(-0.5f, 6.0f, 0.0f), SphereShape{0.25f});
    Group(mate, 4);
    Entity other = AddBody(test.Entities(), Math::Vec3(0.5f, 6.0f, 0.0f), SphereShape{0.25f});
    Group(other, 9);

    log.Run(test, 90);

    INFO("the ball of the trigger's group fell through the zone: it is at y = " << PositionOf(mate).y);
    REQUIRE(PositionOf(mate).y < 1.0f);
    INFO("only the ball of another group entered it: got " << log.enters.size() << " enters");
    REQUIRE(log.enters.size() == 1);
    REQUIRE(log.enters[0].trigger == zone);
    REQUIRE(log.enters[0].otherBody == other);
}

TEST_CASE("A trigger's collision group is its own entity's, not that of the body it is beneath",
          "[physics][groups][triggers]")
{
    PhysicsApp test;
    EventLog log;
    // A kinematic body in group 4, clear of the ball's path, carrying a trigger with no CollisionGroup of its own.
    Entity carrier = AddBody(test.Entities(), Math::Vec3(4.0f, 3.0f, 0.0f), BoxShape{}, MotionType::Kinematic);
    Group(carrier, 4);
    Entity zone = AddTrigger(test.Entities(), Math::Vec3(0.0f), Math::Vec3(1.0f, 0.5f, 1.0f));
    carrier.AddChild(zone);
    zone.GetComponent<Transform>() = Transform(Math::Vec3(-4.0f, 0.0f, 0.0f), identity, Math::Vec3(1.0f));
    Entity ball = AddBody(test.Entities(), Math::Vec3(0.0f, 6.0f, 0.0f), SphereShape{0.25f});
    Group(ball, 4);

    log.Run(test, 90);

    INFO("the trigger is in no group, so a ball of its body's group still enters it: got " << log.enters.size()
                                                                                           << " enters");
    REQUIRE(log.enters.size() == 1);
    REQUIRE(log.enters[0].trigger == zone);
    REQUIRE(log.enters[0].otherBody == ball);
}

TEST_CASE("Giving a trigger the collision group of a body inside it exits that body on the next tick",
          "[physics][groups][triggers]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    Group(box, 4);
    Entity zone = AddTrigger(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f), Math::Vec3(1.0f));
    log.Run(test, 30);
    INFO("the box rests inside the trigger, which is in no group");
    REQUIRE(CountOf(log.enters, box) == 1);
    REQUIRE(CountOf(log.exits, box) == 0);

    Group(zone, 4);
    log.Run(test, 1);
    INFO("the box became one of the trigger's group: it exits on the next tick, and the ground does not");
    REQUIRE(CountOf(log.exits, box) == 1);
    REQUIRE(log.exits.size() == 1);

    zone.GetComponent<CollisionGroup>().group = 0;
    log.Run(test, 1);
    INFO("the trigger left the group: the box enters again on the next tick");
    REQUIRE(CountOf(log.enters, box) == 2);
}

TEST_CASE("Queries ignore collision groups", "[physics][groups][queries]")
{
    PhysicsApp test;
    Entity near = AddPost(test.Entities(), Math::Vec3(0.0f));
    Entity far = AddPost(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    Group(near, 2);
    Group(far, 2);
    test.Run(tick);
    const Math::Vec3 east(1.0f, 0.0f, 0.0f);

    // Looking out from the near post, as game code looking for what its body would meet does: its group-mate is found.
    const std::optional<RaycastHit> ray =
        World(test).Raycast(Math::Vec3(0.0f), east, 10.0f, QueryFilter{.ignoreBody = near});
    INFO("a ray from a grouped body, ignoring it, hits the body of its group beyond");
    REQUIRE(ray.has_value());
    REQUIRE(ray->body == far);

    const Transform pose(Math::Vec3(0.0f), identity, Math::Vec3(1.0f));
    const std::optional<ShapeCastHit> cast =
        World(test).ShapeCast(SphereShape{0.25f}, pose, east, 10.0f, QueryFilter{.ignoreBody = near});
    INFO("so does a shape cast");
    REQUIRE(cast.has_value());
    REQUIRE(cast->body == far);
}

TEST_CASE("Giving a resting pair one collision group ends their contact on the next tick", "[physics][groups][events]")
{
    PhysicsApp test;
    EventLog log;
    const Stack stack = MakeStack(test);
    Entity upper = stack.upper;
    Group(stack.lower, 3);
    Group(upper, 4);
    upper.AddComponent<ContactEvents>();
    log.Run(test, 240);
    INFO("in different groups the upper box rests on the lower, asleep, their contact begun once and not ended");
    REQUIRE(World(test).IsSleeping(upper));
    REQUIRE(CountNaming(log.begins, upper, stack.lower) == 1);
    REQUIRE(log.ends.empty());

    upper.GetComponent<CollisionGroup>().group = 3;
    log.Run(test, 1);

    INFO("one tick after the upper box's group became the lower's, their contact ended: got " << log.ends.size()
                                                                                              << " ends");
    REQUIRE(log.ends.size() == 1);
    REQUIRE(Names(log.ends[0], upper, stack.lower));
    REQUIRE_FALSE(Touching(test, upper, stack.lower));
}

TEST_CASE("Removing a collision group wakes both bodies and begins their contact", "[physics][groups][events]")
{
    PhysicsApp test;
    EventLog log;
    AddGround(test.Entities());
    // Two boxes of one group resting on the ground, most of each inside the other.
    Entity a = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity b = AddBody(test.Entities(), Math::Vec3(0.2f, 0.5f, 0.0f));
    Group(a, 3);
    Group(b, 3);
    a.AddComponent<ContactEvents>();
    log.Run(test, 240);
    INFO("in one group the two rest inside each other, asleep, with no contact between them");
    REQUIRE(World(test).IsSleeping(a));
    REQUIRE(World(test).IsSleeping(b));
    REQUIRE(CountNaming(log.begins, a, b) == 0);

    a.RemoveComponent<CollisionGroup>();
    log.Run(test, 1);

    INFO("one tick after one box left the group, both are awake and their contact has begun");
    REQUIRE_FALSE(World(test).IsSleeping(a));
    REQUIRE_FALSE(World(test).IsSleeping(b));
    REQUIRE(CountNaming(log.begins, a, b) == 1);
    REQUIRE(Touching(test, a, b));
}

TEST_CASE("A grouped body in a joint's excluded pair keeps both rules", "[physics][groups][joints]")
{
    PhysicsApp test;
    // A weightless box overlapping three static boxes: a post it is fixed to, one of its own group, and a stranger.
    Entity post = AddPost(test.Entities(), Math::Vec3(0.0f));
    Entity box = AddBody(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
    box.GetComponent<RigidBody>().gravityScale = 0.0f;
    Entity mate = AddPost(test.Entities(), Math::Vec3(1.6f, 0.0f, 0.0f));
    Entity stranger = AddPost(test.Entities(), Math::Vec3(0.8f, 0.8f, 0.0f));
    Group(box, 6);
    Group(mate, 6);
    Entity joint = Join(test, post, box, Math::Vec3(0.4f, 0.0f, 0.0f));

    test.Run(0.2);
    INFO("the joint keeps the box from the post, and its group from its group-mate; the stranger it touches");
    REQUIRE_FALSE(Touching(test, box, post));
    REQUIRE_FALSE(Touching(test, box, mate));
    REQUIRE(Touching(test, box, stranger));

    joint.GetComponent<Joint>().enabled = false;
    test.app.RunFrame(tick);
    INFO("with the joint disabled the box touches the post, and is still kept from its group-mate");
    REQUIRE(Touching(test, box, post));
    REQUIRE_FALSE(Touching(test, box, mate));

    joint.GetComponent<Joint>().enabled = true;
    box.GetComponent<CollisionGroup>().group = 0;
    test.app.RunFrame(tick);
    INFO("with the joint enabled again and the box out of the group, it touches its old group-mate and not the post");
    REQUIRE_FALSE(Touching(test, box, post));
    REQUIRE(Touching(test, box, mate));
}

TEST_CASE("A joint's two bodies in different collision groups still obey collideConnected", "[physics][groups][joints]")
{
    PhysicsApp test;
    Entity post = AddPost(test.Entities(), Math::Vec3(0.0f));
    Entity box = AddBody(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
    Group(post, 1);
    Group(box, 2);
    Entity joint = Join(test, post, box, Math::Vec3(0.4f, 0.0f, 0.0f));

    test.Run(0.2);
    INFO("in different groups, the joint still keeps its two overlapping bodies from colliding");
    REQUIRE_FALSE(Touching(test, box, post));

    joint.GetComponent<Joint>().collideConnected = true;
    test.app.RunFrame(tick);
    INFO("and with collideConnected they touch from the next tick");
    REQUIRE(Touching(test, box, post));
}

namespace
{
    constexpr float linkHalf = 0.3f;
    constexpr float linkSpacing = 0.5f;
    constexpr int chainCount = 8;
    constexpr float chainSpacing = 0.25f;

    constexpr int linkCount = 8;

    /** Where the links of a row of hanging chains are 2 s after the bottom link of every other chain is kicked
     * towards the next chain. Each link is 0.6 long on 0.5 spacing, so neighbours in a chain overlap, kept apart only
     * by their joints; the chains hang 0.25 apart. If `grouped`, each kicked chain shares a group with the chain it
     * swings into, and passes through it; otherwise each chain is in a group of its own, and hits it. */
    std::vector<Math::Vec3> SwingChains(bool useWorkerThreads, bool grouped = true)
    {
        PhysicsConfig config = StepPosesOnly();
        config.useWorkerThreads = useWorkerThreads;
        PhysicsApp test(config);
        std::vector<Entity> links;
        std::vector<Entity> bottoms;
        for (int c = 0; c < chainCount; ++c)
        {
            const Math::Vec3 hook(chainSpacing * static_cast<float>(c), 10.0f, 0.0f);
            Entity above;
            for (int i = 0; i < linkCount; ++i)
            {
                const Math::Vec3 top = hook - Math::Vec3(0.0f, linkSpacing * static_cast<float>(i), 0.0f);
                Entity link = AddBody(test.Entities(), top - Math::Vec3(0.0f, linkSpacing / 2.0f, 0.0f),
                                      BoxShape{Math::Vec3(0.1f, linkHalf, 0.1f)});
                Group(link, 1u + static_cast<std::uint32_t>(grouped ? c / 2 : c));
                Join(test, link, above, top, BallJoint{});
                links.push_back(link);
                above = link;
            }
            bottoms.push_back(above);
        }
        test.Run(0.2);
        for (std::size_t c = 0; c < bottoms.size(); c += 2)
        {
            World(test).AddImpulse(bottoms[c], Math::Vec3(3.0f + 0.5f * static_cast<float>(c), 0.0f, 0.0f));
        }
        test.Run(2.0);
        std::vector<Math::Vec3> at;
        for (const Entity& link : links)
        {
            at.push_back(PositionOf(link));
        }
        return at;
    }

    bool BitIdentical(const std::vector<Math::Vec3>& a, const std::vector<Math::Vec3>& b)
    {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Math::Vec3)) == 0;
    }
} // namespace

TEST_CASE("Grouped and jointed bodies end bit for bit the same in two runs, on one thread or many",
          "[physics][groups][joints]")
{
    const std::vector<Math::Vec3> single = SwingChains(false);

    // The bottom link of the first chain, which was kicked from x = 0.
    INFO("the kicked chains swung");
    REQUIRE(std::abs(single[linkCount - 1].x) > 0.05f);
    INFO("the groups decided what met: with every chain in a group of its own, the kicked chains hit their "
         "neighbours and the links end elsewhere");
    REQUIRE_FALSE(BitIdentical(single, SwingChains(false, false)));
    INFO("a second run on one thread agrees bit for bit with the first");
    REQUIRE(BitIdentical(single, SwingChains(false)));
    INFO("a run on the worker threads agrees bit for bit with one on one thread");
    REQUIRE(BitIdentical(single, SwingChains(true)));
    INFO("and so does a second run on the worker threads");
    REQUIRE(BitIdentical(single, SwingChains(true)));
}
