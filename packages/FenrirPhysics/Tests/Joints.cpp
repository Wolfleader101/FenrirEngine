// Joints through the frame seam (ADR-0013): bodies joined by Joint components on joint entities, observed in their
// world transforms as physics steps.

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

TEST_CASE("A Joint made with no arguments has the spec's defaults", "[physics][joints]")
{
    const Joint joint;

    INFO("both bodies are empty: bodyB empty is the world");
    REQUIRE(joint.bodyA == Entity());
    REQUIRE(joint.bodyB == Entity());
    INFO("both frames are the identity at the body's origin");
    REQUIRE(joint.frameA.position == Math::Vec3(0.0f));
    REQUIRE(joint.frameA.rotation == identity);
    REQUIRE(joint.frameB.position == Math::Vec3(0.0f));
    REQUIRE(joint.frameB.rotation == identity);
    INFO("the kind is a fixed joint");
    REQUIRE(joint.kind.Type() == JointType::Fixed);
    REQUIRE(joint.kind.Is<FixedJoint>());
    INFO("it is enabled, its bodies do not collide, and it never breaks");
    REQUIRE(joint.enabled);
    REQUIRE_FALSE(joint.collideConnected);
    REQUIRE((std::isinf(joint.breakForce) && joint.breakForce > 0.0f));
    REQUIRE((std::isinf(joint.breakTorque) && joint.breakTorque > 0.0f));
}

TEST_CASE("A joint kind takes any kind by assignment and is read through a checked view", "[physics][joints]")
{
    JointKind kind = BallJoint{};

    INFO("assigning a ball joint makes the kind a ball joint");
    REQUIRE(kind.Type() == JointType::Ball);
    REQUIRE(kind.Is<BallJoint>());
    REQUIRE_FALSE(kind.Is<FixedJoint>());
    REQUIRE_FALSE(kind.Is<HingeJoint>());

    kind = HingeJoint{};
    INFO("assigning a hinge makes it a hinge, read through the view");
    REQUIRE(kind.Type() == JointType::Hinge);
    REQUIRE(kind.Is<HingeJoint>());
    [[maybe_unused]] const HingeJoint& hinge = kind.As<HingeJoint>();
}

namespace
{
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);

    /** Joins `bodyA` to `bodyB` (empty: the world) with a joint of `kind` on `jointEntity`, its rest pose at `anchor`
     * where the bodies are now. */
    Joint& Join(Entity jointEntity, const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor, JointKind kind)
    {
        const JointFrames frames = FrameFromWorld(bodyA, bodyB, anchor);
        return jointEntity.AddComponent<Joint>(
            Joint{.bodyA = bodyA, .bodyB = bodyB, .frameA = frames.frameA, .frameB = frames.frameB, .kind = kind});
    }

    /** A unit box hanging from `hook` by the middle of its top face, joined to the world on a joint entity of its
     * own. */
    Entity HangFromHook(PhysicsApp& test, JointKind kind)
    {
        Entity box = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f));
        Join(test.Entities().CreateEntity(), box, Entity(), JointFrame{hook}, kind);
        return box;
    }
} // namespace

TEST_CASE("Each kind of joint holds a body hung from the world at its rest pose", "[physics][joints]")
{
    for (const JointKind& kind : {JointKind(FixedJoint{}), JointKind(BallJoint{}), JointKind(HingeJoint{})})
    {
        PhysicsApp test;
        Entity box = HangFromHook(test, kind);

        test.Run(2.0);

        const Transform at = GetWorldTransform(box);
        INFO("joint type " << static_cast<int>(kind.Type()) << ": the box hangs from the hook by the middle of its "
                           << "top face, its centre half a unit below the hook, unturned; it is at " << at.pos.x << ", "
                           << at.pos.y << ", " << at.pos.z);
        REQUIRE(at.pos.x == Approx(0.0f).margin(1e-3));
        REQUIRE(at.pos.y == Approx(9.5f).margin(1e-3));
        REQUIRE(at.pos.z == Approx(0.0f).margin(1e-3));
        REQUIRE(std::abs(at.rot.w) == Approx(1.0f).margin(1e-4));
    }
}

TEST_CASE("A fixed joint holds a body by one corner without letting it turn", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), hook - Math::Vec3(-0.5f, 0.5f, 0.0f));
    // The box's top left edge: free to turn, its weight would swing it under the hook.
    Join(test.Entities().CreateEntity(), box, Entity(), JointFrame{hook}, FixedJoint{});

    test.Run(2.0);

    const Transform at = GetWorldTransform(box);
    INFO("the box stays where it was made, beside and below the hook, unturned; it is at "
         << at.pos.x << ", " << at.pos.y << ", " << at.pos.z << " turned by w = " << at.rot.w);
    REQUIRE(at.pos.x == Approx(0.5f).margin(2e-3));
    REQUIRE(at.pos.y == Approx(9.5f).margin(2e-3));
    REQUIRE(std::abs(at.rot.w) == Approx(1.0f).margin(1e-4));
}

namespace
{
    /** Where the point `local` on `entity` (in its own space, before scale) is in the world now. */
    Math::Vec3 WorldPointOf(const Entity& entity, const Math::Vec3& local)
    {
        return Math::MultiplyPoint(local, GetWorldMatrix(entity));
    }

    /** A door 1 wide and 2 tall beside a static post, hinged on its left edge about world up (frame X turned onto Y).
     * The hinge is on the door's own entity: the post is bodyA, the door bodyB. */
    struct Door
    {
        Entity post;
        Entity door;
    };

    Door MakeDoor(PhysicsApp& test)
    {
        Door made;
        made.post = Place(test.Entities(), Math::Vec3(-0.8f, 1.0f, 0.0f));
        made.post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f, 1.0f, 0.1f)}});
        made.door = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), BoxShape{Math::Vec3(0.5f, 1.0f, 0.05f)});
        const Math::Quat axisUp = Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
        Join(made.door, made.post, made.door, JointFrame{Math::Vec3(-0.5f, 1.0f, 0.0f), axisUp}, HingeJoint{});
        return made;
    }
} // namespace

TEST_CASE("A hinge turns its body about the frames' X axis only", "[physics][joints]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test);
    test.Run(0.1);

    // Twisted about every axis: the hinge keeps only the turn about its own, world up.
    PhysicsWorld::Of(test.app.GetActiveScene())->AddAngularImpulse(door.door, Math::Vec3(0.2f, 0.3f, 0.2f));
    test.Run(0.5);

    const Math::Quat turned = GetWorldTransform(door.door).rot;
    INFO("the door turned about world up and nothing else: its rotation is " << turned.w << ", " << turned.x << ", "
                                                                             << turned.y << ", " << turned.z);
    REQUIRE(std::abs(turned.y) > 0.1f);
    REQUIRE(turned.x == Approx(0.0f).margin(1e-3));
    REQUIRE(turned.z == Approx(0.0f).margin(1e-3));
    const Math::Vec3 hinge = WorldPointOf(door.door, Math::Vec3(-0.5f, 0.0f, 0.0f));
    INFO("its left edge stays on the hinge, at -0.5, 1, 0; it is at " << hinge.x << ", " << hinge.y << ", " << hinge.z);
    REQUIRE(Math::Magnitude(hinge - Math::Vec3(-0.5f, 1.0f, 0.0f)) < 1e-3f);
}

TEST_CASE("A ball joint lets its body turn about every axis", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = HangFromHook(test, BallJoint{});
    test.Run(0.1);

    PhysicsWorld::Of(test.app.GetActiveScene())->AddAngularImpulse(box, Math::Vec3(0.2f, 0.3f, 0.2f));
    test.Run(0.2);

    const Math::Quat turned = GetWorldTransform(box).rot;
    INFO("the box turned about all three axes: its rotation is " << turned.w << ", " << turned.x << ", " << turned.y
                                                                 << ", " << turned.z);
    REQUIRE(std::abs(turned.x) > 0.01f);
    REQUIRE(std::abs(turned.y) > 0.01f);
    REQUIRE(std::abs(turned.z) > 0.01f);
    const Math::Vec3 top = WorldPointOf(box, Math::Vec3(0.0f, 0.5f, 0.0f));
    INFO("the middle of its top face stays on the hook; it is at " << top.x << ", " << top.y << ", " << top.z);
    REQUIRE(Math::Magnitude(top - hook) < 1e-3f);
}

namespace
{
    constexpr float linkHalf = 0.25f;

    /** `count` unit-mass links hanging straight down from `hook`, each joined to the one above by a ball joint on its
     * own joint entity, the top one to the world. */
    struct Chain
    {
        std::vector<Entity> links;
        std::vector<Entity> joints;
    };

    Chain MakeChain(EntityList& entities, int count, const Math::Vec3& from = hook)
    {
        Chain chain;
        for (int i = 0; i < count; ++i)
        {
            const Math::Vec3 top = from - Math::Vec3(0.0f, 2.0f * linkHalf * static_cast<float>(i), 0.0f);
            Entity link =
                AddBody(entities, top - Math::Vec3(0.0f, linkHalf, 0.0f), BoxShape{Math::Vec3(0.1f, linkHalf, 0.1f)});
            const Entity above = i == 0 ? Entity() : chain.links.back();
            Entity joint = entities.CreateEntity();
            Join(joint, link, above, JointFrame{top}, BallJoint{});
            chain.links.push_back(link);
            chain.joints.push_back(joint);
        }
        return chain;
    }
} // namespace

TEST_CASE("A body takes part in several joints: a chain hangs link below link", "[physics][joints]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);

    test.Run(3.0);

    for (std::size_t i = 0; i < chain.links.size(); ++i)
    {
        const Math::Vec3 at = PositionOf(chain.links[i]);
        const float expected = hook.y - linkHalf * static_cast<float>(2 * i + 1);
        INFO("link " << i << " hangs " << linkHalf * static_cast<float>(2 * i + 1) << " below the hook, at y = "
                     << expected << "; it is at " << at.x << ", " << at.y << ", " << at.z);
        REQUIRE(at.y == Approx(expected).margin(0.02));
        REQUIRE(at.x == Approx(0.0f).margin(0.01));
        REQUIRE(at.z == Approx(0.0f).margin(0.01));
    }
}

namespace
{
    /**
     * A unit box under a parent at (3, 10, 0), turned a quarter about world up and scaled by 2; the box's own scale is
     * (1, 0.5, 1), so its world scale is (2, 1, 2). Its corner (0.5, 0.5, 0.5) is at (4, 10.5, -1) in the world.
     */
    Entity BoxUnderTurnedScaledParent(PhysicsApp& test)
    {
        Entity parent = test.Entities().CreateEntity();
        parent.GetComponent<Transform>() = Transform(
            Math::Vec3(3.0f, 10.0f, 0.0f),
            Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 1.0f, 0.0f)), Math::Vec3(2.0f));
        Entity box = Place(test.Entities(), Math::Vec3(0.0f), Math::Vec3(1.0f, 0.5f, 1.0f));
        parent.AddChild(box);
        box.AddComponent<RigidBody>();
        box.AddComponent<Collider>();
        return box;
    }

    const Math::Vec3 corner(0.5f, 0.5f, 0.5f);
    const Math::Vec3 cornerInWorld(4.0f, 10.5f, -1.0f);
} // namespace

TEST_CASE("A frame on a body under a turned, scaled parent stays on the body as it swings", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = BoxUnderTurnedScaledParent(test);
    REQUIRE(Math::Magnitude(WorldPointOf(box, corner) - cornerInWorld) < 1e-5f);
    // Written by hand: the frame is the box's corner in its own space, which its scale carries to the corner.
    test.Entities().CreateEntity().AddComponent<Joint>(Joint{.bodyA = box,
                                                             .bodyB = Entity(),
                                                             .frameA = JointFrame{corner},
                                                             .frameB = JointFrame{cornerInWorld},
                                                             .kind = BallJoint{}});

    for (int second = 0; second < 3; ++second)
    {
        test.Run(1.0);
        const Math::Vec3 at = WorldPointOf(box, corner);
        INFO("after " << second + 1 << " s of swinging by its corner, the corner is still at 4, 10.5, -1; it is at "
                      << at.x << ", " << at.y << ", " << at.z);
        REQUIRE(Math::Magnitude(at - cornerInWorld) < 0.01f);
    }
    INFO("it swung: its centre is no longer where it was made");
    REQUIRE(Math::Magnitude(PositionOf(box) - Math::Vec3(3.0f, 10.0f, 0.0f)) > 0.1f);
}

TEST_CASE("FrameFromWorld gives the anchor in each body's own space", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = BoxUnderTurnedScaledParent(test);
    Entity other = AddBody(test.Entities(), Math::Vec3(4.0f, 12.0f, -1.0f));
    const Math::Quat quarterUp = Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 1.0f, 0.0f));

    const JointFrames frames = FrameFromWorld(box, other, JointFrame{cornerInWorld, quarterUp});

    INFO("on the box, the anchor is its corner (0.5, 0.5, 0.5), before scale; it is at "
         << frames.frameA.position.x << ", " << frames.frameA.position.y << ", " << frames.frameA.position.z);
    REQUIRE(Math::Magnitude(frames.frameA.position - corner) < 1e-5f);
    INFO("turned as the box is turned, the anchor is unturned in the box's space");
    REQUIRE(std::abs(frames.frameA.rotation.w) == Approx(1.0f).margin(1e-5));
    INFO("on the other box, unscaled and unturned, it is 2 below its centre, turned a quarter");
    REQUIRE(Math::Magnitude(frames.frameB.position - Math::Vec3(0.0f, -1.5f, 0.0f)) < 1e-5f);
    REQUIRE(frames.frameB.rotation.w == Approx(std::sqrt(0.5f)).margin(1e-5));
    REQUIRE(frames.frameB.rotation.y == Approx(std::sqrt(0.5f)).margin(1e-5));
}

TEST_CASE("FrameFromWorld treats only an empty bodyB as the world", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(1.0f, 2.0f, 3.0f));
    Entity gone = AddBody(test.Entities(), Math::Vec3(0.0f));
    test.Entities().DestroyEntity(gone);
    const JointFrame anchor{Math::Vec3(1.0f, 2.5f, 3.0f)};

    const JointFrames toWorld = FrameFromWorld(box, Entity(), anchor);
    INFO("with an empty bodyB, frameB is the anchor itself, in world space");
    REQUIRE(toWorld.frameB.position == anchor.position);
    REQUIRE(toWorld.frameB.rotation == anchor.rotation);
    REQUIRE(Math::Magnitude(toWorld.frameA.position - Math::Vec3(0.0f, 0.5f, 0.0f)) < 1e-5f);
    REQUIRE(test.warnings.empty());

    const JointFrames toDestroyed = FrameFromWorld(box, gone, anchor);
    INFO("a destroyed bodyB is not the world: both frames are the identity");
    REQUIRE(toDestroyed.frameA.position == Math::Vec3(0.0f));
    REQUIRE(toDestroyed.frameB.position == Math::Vec3(0.0f));
#ifndef NDEBUG
    INFO("with one warning, in a debug build");
    REQUIRE(test.warnings.size() == 1);
#endif
}

TEST_CASE("FrameFromWorld with a bodyA that is not alive gives identity frames", "[physics][joints]")
{
    PhysicsApp test;
    Entity gone = AddBody(test.Entities(), Math::Vec3(0.0f));
    test.Entities().DestroyEntity(gone);

    for (const Entity& bodyA : {gone, Entity()})
    {
        const JointFrames frames = FrameFromWorld(bodyA, Entity(), JointFrame{Math::Vec3(1.0f, 2.0f, 3.0f)});
        INFO("both frames are the identity");
        REQUIRE(frames.frameA.position == Math::Vec3(0.0f));
        REQUIRE(frames.frameA.rotation == identity);
        REQUIRE(frames.frameB.position == Math::Vec3(0.0f));
        REQUIRE(frames.frameB.rotation == identity);
    }
#ifndef NDEBUG
    INFO("with one warning each, in a debug build");
    REQUIRE(test.warnings.size() == 2);
#endif
}

namespace
{
    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    Joint& JointOf(const Entity& entity)
    {
        return entity.GetComponent<Joint>();
    }

    /** The joint entity of the box HangFromHook hung: the one made after it. */
    Entity HangingJoint(PhysicsApp& test, const Entity& box)
    {
        return test.Entities().GetEntity(box.GetId() + 1);
    }
} // namespace

TEST_CASE("Disabling a joint reaches physics on the next tick", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = HangFromHook(test, BallJoint{});
    test.Run(1.0);
    REQUIRE(std::abs(box.GetComponent<RigidBody>().linearVelocity.y) < 0.01f);

    JointOf(HangingJoint(test, box)).enabled = false;
    test.app.RunFrame(tick);

    const float fall = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("one tick after the joint was disabled, the box falls at one tick of gravity, 9.81 / 60; it moves at "
         << fall);
    REQUIRE(fall == Approx(-9.81f / 60.0f).margin(0.01));
}

TEST_CASE("Changing a joint's kind recreates it at the same rest pose", "[physics][joints]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test);
    test.Run(0.1);
    World(test).AddAngularImpulse(door.door, Math::Vec3(0.0f, 0.3f, 0.0f));
    test.Run(0.5);
    REQUIRE(std::abs(GetWorldTransform(door.door).rot.y) > 0.1f);

    JointOf(door.door).kind = FixedJoint{};
    test.Run(2.0);

    const Transform at = GetWorldTransform(door.door);
    INFO("the open door, now fixed to its post, is pulled back to where the frames coincide: closed, at 0, 1, 0; it is "
         "at "
         << at.pos.x << ", " << at.pos.y << ", " << at.pos.z << ", turned by w = " << at.rot.w);
    REQUIRE(Math::Magnitude(at.pos - Math::Vec3(0.0f, 1.0f, 0.0f)) < 0.01f);
    REQUIRE(std::abs(at.rot.w) == Approx(1.0f).margin(1e-4));
}

TEST_CASE("Changing a joint's frame recreates it at the new rest pose", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = HangFromHook(test, FixedJoint{});
    test.Run(0.5);

    JointOf(HangingJoint(test, box)).frameB.position = hook + Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(2.0);

    const Math::Vec3 at = PositionOf(box);
    INFO("the box hangs below the moved world frame, at 1, 9.5, 0; it is at " << at.x << ", " << at.y << ", " << at.z);
    REQUIRE(Math::Magnitude(at - Math::Vec3(1.0f, 9.5f, 0.0f)) < 0.01f);
}

TEST_CASE("Changing a joint's body recreates it against the new body", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = HangFromHook(test, FixedJoint{});
    Entity post = Place(test.Entities(), Math::Vec3(2.0f, 11.0f, 0.0f));
    post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f, 0.5f, 0.1f)}});
    test.Run(0.5);

    Joint& joint = JointOf(HangingJoint(test, box));
    joint.bodyB = post;
    joint.frameB = JointFrame{Math::Vec3(0.0f, -1.0f, 0.0f)};
    test.Run(2.0);

    const Math::Vec3 at = PositionOf(box);
    INFO("the box hangs from the post's frame a unit below the post, at 2, 9.5, 0; it is at " << at.x << ", " << at.y
                                                                                              << ", " << at.z);
    REQUIRE(Math::Magnitude(at - Math::Vec3(2.0f, 9.5f, 0.0f)) < 0.01f);
}

TEST_CASE("Changing a body's scale recreates its joints, the frames following the scale", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = HangFromHook(test, FixedJoint{});
    test.Run(0.5);

    // The frame is the middle of the box's top face, (0, 0.5, 0) in its own space: twice as tall, it is 1 above the
    // box's centre.
    box.GetComponent<Transform>().scale = Math::Vec3(1.0f, 2.0f, 1.0f);
    test.Run(1.0);

    const Math::Vec3 at = PositionOf(box);
    INFO("the taller box hangs by its top face, its centre a unit below the hook; it is at " << at.x << ", " << at.y
                                                                                             << ", " << at.z);
    REQUIRE(Math::Magnitude(at - Math::Vec3(0.0f, 9.0f, 0.0f)) < 0.01f);
}

namespace
{
    /** Where a six-link chain's links are 3 s after its bottom link is kicked, with `edit` applied to the chain after
     * 1 s. `before` runs first, so what it makes comes before the chain in entity order. */
    template <typename Edit, typename Before>
    std::vector<Math::Vec3> SwingChain(Edit edit, Before before, const PhysicsConfig& config = StepPosesOnly())
    {
        PhysicsApp test(config);
        before(test);
        const Chain chain = MakeChain(test.Entities(), 6);
        test.Run(0.2);
        World(test).AddImpulse(chain.links.back(), Math::Vec3(4.0f, 0.0f, 2.0f));
        test.Run(0.8);
        edit(test, chain);
        test.Run(2.0);
        std::vector<Math::Vec3> at;
        for (const Entity& link : chain.links)
        {
            at.push_back(PositionOf(link));
        }
        return at;
    }

    template <typename Edit>
    std::vector<Math::Vec3> SwingChain(Edit edit)
    {
        return SwingChain(edit, [](PhysicsApp&) {});
    }

    bool BitIdentical(const std::vector<Math::Vec3>& a, const std::vector<Math::Vec3>& b)
    {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Math::Vec3)) == 0;
    }
} // namespace

TEST_CASE("A Joint removed and added again in one tick is rebuilt", "[physics][joints]")
{
    const std::vector<Math::Vec3> untouched = SwingChain([](PhysicsApp&, const Chain&) {});
    const std::vector<Math::Vec3> readded = SwingChain([](PhysicsApp&, const Chain& chain) {
        Entity joint = chain.joints[2];
        const Joint kept = JointOf(joint);
        joint.RemoveComponent<Joint>();
        joint.AddComponent<Joint>(kept);
    });

    INFO("the rebuilt joint starts again without the last step's solution, so the chain swings differently from one "
         "left alone");
    REQUIRE_FALSE(BitIdentical(untouched, readded));
    for (std::size_t i = 0; i < readded.size(); ++i)
    {
        const float below = hook.y - readded[i].y;
        INFO("link " << i << " still hangs from the one above, at most " << linkHalf * static_cast<float>(2 * i + 1)
                     << " below the hook; it is " << below << " below");
        REQUIRE(below < linkHalf * static_cast<float>(2 * i + 1) + 0.03f);
    }
}

TEST_CASE("A Joint added and removed again in one tick does nothing", "[physics][joints]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity joint = test.Entities().CreateEntity();
    Join(joint, box, Entity(), JointFrame{hook}, BallJoint{});
    joint.RemoveComponent<Joint>();

    test.Run(1.0);

    INFO("the box fell freely, about 4.9 in a second; it is at y = " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y < 9.5f - 4.5f);
}

TEST_CASE("Every change to a joint wakes both its sleeping bodies", "[physics][joints]")
{
    struct Change
    {
        const char* what;
        void (*apply)(Joint&);
    };
    const Change changes[] = {
        {"disabling it", [](Joint& joint) { joint.enabled = false; }},
        {"changing its kind", [](Joint& joint) { joint.kind = HingeJoint{}; }},
        {"moving a frame", [](Joint& joint) { joint.frameA.position.x += 0.1f; }},
        {"letting its bodies collide", [](Joint& joint) { joint.collideConnected = true; }},
        {"lowering its break force", [](Joint& joint) { joint.breakForce = 1000.0f; }},
    };
    for (const Change& change : changes)
    {
        PhysicsApp test;
        const Chain chain = MakeChain(test.Entities(), 2);
        test.Run(3.0);
        REQUIRE(World(test).IsSleeping(chain.links[0]));
        REQUIRE(World(test).IsSleeping(chain.links[1]));

        change.apply(JointOf(chain.joints[1]));
        test.app.RunFrame(tick);

        INFO(change.what << " between the two links wakes both");
        REQUIRE_FALSE(World(test).IsSleeping(chain.links[0]));
        REQUIRE_FALSE(World(test).IsSleeping(chain.links[1]));
    }
}

namespace
{
    /** Two boxes resting on the ground far from the chain, and an entity that `join` makes a ball joint between them:
     * all made before the chain, so first in entity order. */
    struct Spare
    {
        Entity left;
        Entity right;
        Entity joint;
    };

    Spare MakeSpare(PhysicsApp& test, bool join)
    {
        AddGround(test.Entities());
        Spare spare;
        spare.left = AddBody(test.Entities(), Math::Vec3(-5.0f, 0.5f, 0.0f));
        spare.right = AddBody(test.Entities(), Math::Vec3(-3.8f, 0.5f, 0.0f));
        spare.joint = test.Entities().CreateEntity();
        if (join)
        {
            Join(spare.joint, spare.left, spare.right, JointFrame{Math::Vec3(-4.4f, 0.5f, 0.0f)}, BallJoint{});
        }
        return spare;
    }
} // namespace

TEST_CASE("Removing an earlier joint leaves a chain's motion as if that joint had never been made", "[physics][joints]")
{
    Spare spare;
    const std::vector<Math::Vec3> removed =
        SwingChain([&spare](PhysicsApp&, const Chain&) { spare.joint.RemoveComponent<Joint>(); },
                   [&spare](PhysicsApp& test) { spare = MakeSpare(test, true); });
    const std::vector<Math::Vec3> neverMade =
        SwingChain([](PhysicsApp&, const Chain&) {}, [](PhysicsApp& test) { MakeSpare(test, false); });

    INFO("the chain swung");
    REQUIRE(std::abs(removed.back().x) > 0.05f);
    INFO("the chain's joints solve in joint entity order, whatever the backend does with the removed joint's place: "
         "the chain ends bit for bit where it ends when the spare joint never existed");
    REQUIRE(BitIdentical(removed, neverMade));
}

TEST_CASE("A swinging chain ends bit for bit the same on every run, on one thread or many", "[physics][joints]")
{
    const auto none = [](PhysicsApp&, const Chain&) {};
    const auto nothing = [](PhysicsApp&) {};
    PhysicsConfig single = StepPosesOnly();
    single.useWorkerThreads = false;
    PhysicsConfig workers = StepPosesOnly();
    workers.useWorkerThreads = true;

    const std::vector<Math::Vec3> first = SwingChain(none, nothing, single);
    INFO("two runs on one thread agree");
    REQUIRE(BitIdentical(first, SwingChain(none, nothing, single)));
    INFO("a run on the worker threads agrees with one on one thread");
    REQUIRE(BitIdentical(first, SwingChain(none, nothing, workers)));
}

TEST_CASE("A joint on one of its own bodies holds it", "[physics][joints]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test);

    test.Run(2.0);

    const Math::Vec3 at = PositionOf(door.door);
    INFO("the door holds its own hinge to the post, and hangs where it was made, at 0, 1, 0; it is at "
         << at.x << ", " << at.y << ", " << at.z);
    REQUIRE(Math::Magnitude(at - Math::Vec3(0.0f, 1.0f, 0.0f)) < 2e-3f);
}

TEST_CASE("Destroying a jointed body mid-swing removes its joints with it", "[physics][joints]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 6);
    test.Run(0.5);
    World(test).AddImpulse(chain.links.back(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(0.3);

    test.Entities().DestroyEntity(chain.links[2]);
    test.Run(2.0);

    INFO("nothing went wrong in the backend");
    REQUIRE(test.errors.empty());
    INFO("the links below the destroyed one fell; the bottom one is at y = " << PositionOf(chain.links.back()).y);
    REQUIRE(PositionOf(chain.links.back()).y < hook.y - 10.0f);
    INFO("the links above still hang; the second is at y = " << PositionOf(chain.links[1]).y);
    REQUIRE(PositionOf(chain.links[1]).y == Approx(hook.y - 3.0f * linkHalf).margin(0.1));
}

TEST_CASE("A jointed body rebuilt by a collider added beneath it keeps its joints", "[physics][joints]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 4);
    test.Run(1.0);

    // A small lump beneath link 1 makes it a compound body: the backend body is made again.
    Entity lump = Place(test.Entities(), Math::Vec3(0.0f));
    chain.links[1].AddChild(lump);
    lump.GetComponent<Transform>() = Transform(Math::Vec3(0.0f), identity, Math::Vec3(1.0f));
    lump.AddComponent<Collider>(Collider{SphereShape{0.05f}});
    test.Run(2.0);

    REQUIRE(test.errors.empty());
    for (std::size_t i = 0; i < chain.links.size(); ++i)
    {
        const float expected = hook.y - linkHalf * static_cast<float>(2 * i + 1);
        INFO("link " << i << " still hangs at y = " << expected << "; it is at " << PositionOf(chain.links[i]).y);
        REQUIRE(PositionOf(chain.links[i]).y == Approx(expected).margin(0.02));
    }
}
