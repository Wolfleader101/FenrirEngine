// Jointed bodies not colliding, through the frame seam: the two bodies a joint names do not touch each other unless
// the joint sets collideConnected, observed in their contacts and events as physics steps.

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;

namespace
{
    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    /** Whether `a`'s body touches `b`'s now, as of the last step. */
    bool Touching(PhysicsApp& test, const Entity& a, const Entity& b)
    {
        bool touching = false;
        World(test).ForEachContact(a, [&](const Contact& contact) { touching = touching || contact.otherBody == b; });
        return touching;
    }

    /** A joint of `kind` on a new joint entity between `bodyA` and `bodyB`, its rest pose at `anchor` where the bodies
     * are now. */
    Entity Join(PhysicsApp& test, const Entity& bodyA, const Entity& bodyB, const Math::Vec3& anchor,
                JointKind kind = FixedJoint{}, bool collideConnected = false)
    {
        Entity joint = test.Entities().CreateEntity();
        const JointFrames frames = FrameFromWorld(bodyA, bodyB, JointFrame{anchor});
        joint.AddComponent<Joint>(Joint{.bodyA = bodyA,
                                        .bodyB = bodyB,
                                        .frameA = frames.frameA,
                                        .frameB = frames.frameB,
                                        .kind = kind,
                                        .collideConnected = collideConnected});
        return joint;
    }

    /** A static unit box at the origin, and a dynamic unit box overlapping it by 0.2 along x. */
    struct Pair
    {
        Entity post;
        Entity box;
    };

    Pair MakeOverlappingPair(PhysicsApp& test)
    {
        Pair pair;
        pair.post = Place(test.Entities(), Math::Vec3(0.0f));
        pair.post.AddComponent<Collider>();
        pair.box = AddBody(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
        return pair;
    }

    /** The overlapping pair, held where it is by a fixed joint between them. */
    struct JoinedPair : Pair
    {
        Entity joint;
    };

    JoinedPair MakeJoinedPair(PhysicsApp& test, bool collideConnected)
    {
        JoinedPair pair;
        static_cast<Pair&>(pair) = MakeOverlappingPair(test);
        pair.joint = Join(test, pair.post, pair.box, Math::Vec3(0.4f, 0.0f, 0.0f), FixedJoint{}, collideConnected);
        return pair;
    }

    Joint& JointOf(const Entity& entity)
    {
        return entity.GetComponent<Joint>();
    }
} // namespace

TEST_CASE("Jointed bodies do not collide with each other by default", "[physics][joints][collide]")
{
    PhysicsApp test;
    const JoinedPair pair = MakeJoinedPair(test, false);

    test.Run(0.2);

    INFO("the post and the box overlap, but the joint between them excludes their contact");
    REQUIRE_FALSE(Touching(test, pair.post, pair.box));
    REQUIRE_FALSE(Touching(test, pair.box, pair.post));
}

TEST_CASE("A joint with collideConnected lets its bodies collide", "[physics][joints][collide]")
{
    PhysicsApp test;
    const JoinedPair pair = MakeJoinedPair(test, true);

    test.Run(0.2);

    INFO("the post and the box overlap and the joint lets them touch");
    REQUIRE(Touching(test, pair.post, pair.box));
}

TEST_CASE("Bodies two apart in a chain still collide", "[physics][joints][collide]")
{
    PhysicsApp test;
    // A static post, a box fixed above it clear of it, and a box fixed to that one, overlapping the post.
    Entity post = Place(test.Entities(), Math::Vec3(0.0f));
    post.AddComponent<Collider>();
    Entity middle = AddBody(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    Entity end = AddBody(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
    Join(test, post, middle, Math::Vec3(0.0f, 0.75f, 0.0f));
    Join(test, middle, end, Math::Vec3(0.8f, 1.5f, 0.0f));

    test.Run(0.2);

    INFO("no joint names the post and the end box together, so their overlap is a contact");
    REQUIRE(Touching(test, post, end));
}

TEST_CASE("The layer rule wins over collideConnected", "[physics][joints][collide]")
{
    PhysicsApp test;
    const JoinedPair pair = MakeJoinedPair(test, true);
    Entity box = pair.box;
    box.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 1, .mask = 1u << 1});

    test.Run(0.2);

    INFO("the box's mask leaves out the post's layer: collideConnected cannot add the contact back");
    REQUIRE_FALSE(Touching(test, pair.post, pair.box));
}

TEST_CASE("Every solid collider of a jointed body is excluded", "[physics][joints][collide]")
{
    PhysicsApp test;
    // A compound box: its own collider clear of the post, and a child collider overlapping it.
    Entity post = Place(test.Entities(), Math::Vec3(0.0f));
    post.AddComponent<Collider>();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    Entity lump = Place(test.Entities(), Math::Vec3(0.0f));
    box.AddChild(lump);
    lump.GetComponent<Transform>() = Transform(Math::Vec3(0.0f, -1.0f, 0.0f), identity, Math::Vec3(1.0f));
    lump.AddComponent<Collider>(Collider{SphereShape{0.3f}});
    Join(test, post, box, Math::Vec3(0.0f, 0.75f, 0.0f));

    test.Run(0.2);

    INFO("the child collider overlaps the post, but it is part of the jointed box");
    REQUIRE_FALSE(Touching(test, post, box));
}

TEST_CASE("A trigger child of a jointed body still reports the other body", "[physics][joints][collide][triggers]")
{
    PhysicsApp test;
    Entity post = Place(test.Entities(), Math::Vec3(0.0f));
    post.AddComponent<Collider>();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f));
    box.AddChild(zone);
    zone.GetComponent<Transform>() = Transform(Math::Vec3(0.0f, -1.0f, 0.0f), identity, Math::Vec3(1.0f));
    zone.AddComponent<Collider>(Collider{SphereShape{0.3f}});
    zone.AddComponent<Trigger>();
    Join(test, post, box, Math::Vec3(0.0f, 0.75f, 0.0f));

    EventReader<TriggerEnter> reader;
    std::vector<TriggerEnter> enters;
    for (int i = 0; i < 12; ++i)
    {
        test.app.RunFrame(tick);
        for (const TriggerEnter& enter : reader.Read(test.app))
        {
            enters.push_back(enter);
        }
    }

    INFO("the trigger is its own body, which no joint names: it enters the post it overlaps, got " << enters.size()
                                                                                                   << " enters");
    REQUIRE(enters.size() == 1);
    REQUIRE(enters[0].trigger == zone);
    REQUIRE(enters[0].otherBody == post);
}

TEST_CASE("With several joints on one pair, any one without collideConnected excludes it", "[physics][joints][collide]")
{
    for (const bool firstCollides : {false, true})
    {
        PhysicsApp test;
        const Pair pair = MakeOverlappingPair(test);
        Entity first = Join(test, pair.post, pair.box, Math::Vec3(0.4f, 0.0f, 0.0f), FixedJoint{}, firstCollides);
        Entity second = Join(test, pair.post, pair.box, Math::Vec3(0.4f, 0.0f, 0.0f), FixedJoint{}, !firstCollides);

        test.Run(0.2);
        INFO("one of the two joints, the " << (firstCollides ? "second" : "first") << ", excludes the pair");
        REQUIRE_FALSE(Touching(test, pair.post, pair.box));

        JointOf(firstCollides ? second : first).collideConnected = true;
        test.app.RunFrame(tick);
        INFO("once both let them collide, they touch from the next step");
        REQUIRE(Touching(test, pair.post, pair.box));
    }
}

TEST_CASE("A joint to the world excludes nothing", "[physics][joints][collide]")
{
    PhysicsApp test;
    const Pair pair = MakeOverlappingPair(test);
    Join(test, pair.box, Entity(), Math::Vec3(0.8f, 0.5f, 0.0f));

    test.Run(0.2);

    INFO("the box is joined to the world, not to the post: their overlap is a contact");
    REQUIRE(Touching(test, pair.post, pair.box));
}

TEST_CASE("Disabling a joint lets its bodies collide from the next step", "[physics][joints][collide]")
{
    PhysicsApp test;
    const JoinedPair pair = MakeJoinedPair(test, false);
    test.Run(0.2);
    REQUIRE_FALSE(Touching(test, pair.post, pair.box));

    JointOf(pair.joint).enabled = false;
    test.app.RunFrame(tick);
    INFO("one step after the joint was disabled, the overlapping bodies touch");
    REQUIRE(Touching(test, pair.post, pair.box));

    JointOf(pair.joint).enabled = true;
    test.app.RunFrame(tick);
    INFO("one step after it was enabled again, they do not");
    REQUIRE_FALSE(Touching(test, pair.post, pair.box));
}

TEST_CASE("Toggling collideConnected between touching bodies ends their contact, then begins it again",
          "[physics][joints][collide][events]")
{
    PhysicsApp test;
    const JoinedPair pair = MakeJoinedPair(test, true);
    Entity box = pair.box;
    box.AddComponent<ContactEvents>();
    EventReader<ContactBegin> begins;
    EventReader<ContactEnd> ends;
    std::size_t begun = 0;
    std::size_t ended = 0;
    const auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            test.app.RunFrame(tick);
            begun += begins.Read(test.app).size();
            ended += ends.Read(test.app).size();
        }
    };
    run(12);
    REQUIRE(begun == 1);
    REQUIRE(ended == 0);

    JointOf(pair.joint).collideConnected = false;
    run(1);
    INFO("excluding the pair ends their contact on the next step");
    REQUIRE(ended == 1);

    JointOf(pair.joint).collideConnected = true;
    run(1);
    INFO("letting them collide again begins it again on the next step");
    REQUIRE(begun == 2);
    REQUIRE(ended == 1);
}

TEST_CASE("Queries ignore joints", "[physics][joints][collide][queries]")
{
    PhysicsApp test;
    const JoinedPair pair = MakeJoinedPair(test, false);
    test.Run(0.2);

    // From the post, past itself: the jointed box is found where it overlaps the post.
    std::vector<OverlapHit> hits(4);
    const std::size_t found = World(test).Overlap(BoxShape{}, Transform(Math::Vec3(0.0f), identity, Math::Vec3(1.0f)),
                                                  hits, QueryFilter{.ignoreBody = pair.post});

    INFO("an overlap from the post ignoring it finds the box joined to it, got " << found << " hits");
    REQUIRE(found == 1);
    REQUIRE(hits[0].body == pair.box);
}

namespace
{
    constexpr float linkHalf = 0.3f;
    constexpr float linkSpacing = 0.5f;

    /** Where the links of several chains are 2 s after their bottom links are kicked. Each link is 0.6 long on 0.5
     * spacing, so neighbours overlap, held apart only by their joints' exclusion. */
    std::vector<Math::Vec3> SwingOverlappingChains(bool useWorkerThreads)
    {
        PhysicsConfig config = StepPosesOnly();
        config.useWorkerThreads = useWorkerThreads;
        PhysicsApp test(config);
        std::vector<Entity> links;
        std::vector<Entity> bottoms;
        for (int c = 0; c < 8; ++c)
        {
            const Math::Vec3 hook(2.0f * static_cast<float>(c), 10.0f, 0.0f);
            Entity above;
            for (int i = 0; i < 8; ++i)
            {
                const Math::Vec3 top = hook - Math::Vec3(0.0f, linkSpacing * static_cast<float>(i), 0.0f);
                Entity link = AddBody(test.Entities(), top - Math::Vec3(0.0f, linkSpacing / 2.0f, 0.0f),
                                      BoxShape{Math::Vec3(0.1f, linkHalf, 0.1f)});
                Join(test, link, above, top, BallJoint{});
                links.push_back(link);
                above = link;
            }
            bottoms.push_back(above);
        }
        test.Run(0.2);
        for (std::size_t c = 0; c < bottoms.size(); ++c)
        {
            World(test).AddImpulse(bottoms[c], Math::Vec3(3.0f, 0.0f, 0.5f * static_cast<float>(c)));
        }
        test.Run(2.0);
        std::vector<Math::Vec3> at;
        for (const Entity& link : links)
        {
            at.push_back(PositionOf(link));
        }
        return at;
    }
} // namespace

TEST_CASE("Chains with excluded pairs end bit for bit the same on one thread or many", "[physics][joints][collide]")
{
    const std::vector<Math::Vec3> single = SwingOverlappingChains(false);
    const std::vector<Math::Vec3> workers = SwingOverlappingChains(true);

    INFO("the chains swung");
    REQUIRE(std::abs(single.back().x - 14.0f) > 0.05f);
    INFO("a run on the worker threads agrees bit for bit with one on one thread");
    REQUIRE(single.size() == workers.size());
    REQUIRE(std::memcmp(single.data(), workers.data(), single.size() * sizeof(Math::Vec3)) == 0);
}
