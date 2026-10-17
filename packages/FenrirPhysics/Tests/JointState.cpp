// Joint state through the frame seam: PhysicsWorld::GetJointState read after whole ticks, as game code reads it.

#include <cmath>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);
    // Jolt's default gravity, which PhysicsConfig keeps.
    constexpr float g = 9.81f;

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    /** Joins `bodyA` to `bodyB` (empty: the world) with a joint of `kind` on `jointEntity`, its rest pose at `anchor`
     * where the bodies are now. */
    Joint& Join(Entity jointEntity, const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor, JointKind kind)
    {
        const JointFrames frames = FrameFromWorld(bodyA, bodyB, anchor);
        return jointEntity.AddComponent<Joint>(
            Joint{.bodyA = bodyA, .bodyB = bodyB, .frameA = frames.frameA, .frameB = frames.frameB, .kind = kind});
    }

    constexpr float linkHalf = 0.25f;

    /** `count` unit-mass links hanging straight down from `hook`, each joined to the one above by a ball joint on its
     * own joint entity, the top one to the world (bodyB). */
    struct Chain
    {
        std::vector<Entity> links;
        std::vector<Entity> joints;
    };

    Chain MakeChain(EntityList& entities, int count)
    {
        Chain chain;
        for (int i = 0; i < count; ++i)
        {
            const Math::Vec3 top = hook - Math::Vec3(0.0f, 2.0f * linkHalf * static_cast<float>(i), 0.0f);
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

TEST_CASE("A hanging chain's top joint reads the chain's weight", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    test.Run(3.0);

    const JointState state = World(test).GetJointState(chain.joints[0]);
    INFO("the top joint pulls the world (its bodyB) down with five 1 kg links' weight, 49.0 N; it reads "
         << state.force.x << ", " << state.force.y << ", " << state.force.z);
    REQUIRE(state.force.y == Approx(-5.0f * g).margin(0.1));
    REQUIRE(state.force.x == Approx(0.0f).margin(0.05));
    REQUIRE(state.force.z == Approx(0.0f).margin(0.05));
    INFO("a ball joint carries no torque");
    REQUIRE(Math::Magnitude(state.torque) == Approx(0.0f).margin(1e-3));
}

namespace
{
    /** A turn of `angle` about world up. */
    Math::Quat AboutUp(float angle)
    {
        return Math::AngleAxis(angle, Math::Vec3(0.0f, 1.0f, 0.0f));
    }

    const Math::Vec3 hingeLine(-0.5f, 1.0f, 0.0f);

    /** A door 1 wide and 2 tall, its left edge on `hingeLine`. */
    Entity MakeDoorPanel(PhysicsApp& test)
    {
        return AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), BoxShape{Math::Vec3(0.5f, 1.0f, 0.05f)});
    }

    /** Places the door turned by `angle` about its left edge, from where it was made. */
    void TurnDoor(Entity& door, float angle)
    {
        door.GetComponent<Transform>().pos = hingeLine + Math::Rotate(AboutUp(angle), Math::Vec3(0.5f, 0.0f, 0.0f));
        door.GetComponent<Transform>().rot = AboutUp(angle);
    }

    // The frames' X turned onto world up: the hinge turns about world up, along the door's left edge.
    const JointFrame hingeAnchor{hingeLine, Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0, 0, 1))};
} // namespace

TEST_CASE("A hinge reads its angle, signed by which body is A", "[physics][joints][joint-state]")
{
    const float opened = 0.7f;

    PhysicsApp toPost;
    Entity post = Place(toPost.Entities(), Math::Vec3(-0.8f, 1.0f, 0.0f));
    post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f, 1.0f, 0.1f)}});
    Entity door = MakeDoorPanel(toPost);
    Join(door, post, door, hingeAnchor, HingeJoint{});
    TurnDoor(door, opened);
    toPost.Run(0.5);
    INFO("a door opened 0.7 rad about its hinge, the post as bodyA, reads +0.7; it reads "
         << World(toPost).GetJointState(door).position);
    REQUIRE(World(toPost).GetJointState(door).position == Approx(opened).margin(2e-3));

    PhysicsApp toWorld;
    Entity hung = MakeDoorPanel(toWorld);
    Join(hung, hung, Entity(), hingeAnchor, HingeJoint{});
    TurnDoor(hung, opened);
    toWorld.Run(0.5);
    INFO("the same door hinged to the world, which is always bodyB, reads -0.7; it reads "
         << World(toWorld).GetJointState(hung).position);
    REQUIRE(World(toWorld).GetJointState(hung).position == Approx(-opened).margin(2e-3));
}

TEST_CASE("A hinge's angle is wrapped to a half turn either way, with no count of turns",
          "[physics][joints][joint-state]")
{
    const float pi = std::numbers::pi_v<float>;
    PhysicsApp test;
    Entity post = Place(test.Entities(), Math::Vec3(-0.8f, 1.0f, 0.0f));
    post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f, 1.0f, 0.1f)}});
    Entity door = MakeDoorPanel(test);
    Join(door, post, door, hingeAnchor, HingeJoint{});
    TurnDoor(door, 1.5f * pi + 2.0f * pi);
    test.Run(0.5);

    const float angle = World(test).GetJointState(door).position;
    INFO("a door turned one and three quarter turns reads a quarter turn back, -pi/2; it reads " << angle);
    REQUIRE(angle == Approx(-pi / 2.0f).margin(2e-3));
}

TEST_CASE("A slider reads its offset along frameA's X", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    Entity rail = Place(test.Entities(), hook - Math::Vec3(0.0f, 0.0f, 2.0f));
    rail.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f)}});
    Entity box = AddBody(test.Entities(), hook);
    // The frames' X turned onto world Z: level, so gravity does not move the box along it.
    const Math::Quat alongZ = Math::AngleAxis(-std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 1.0f, 0.0f));
    Entity joint = test.Entities().CreateEntity();
    Join(joint, rail, box, JointFrame{hook, alongZ}, SliderJoint{});
    box.GetComponent<Transform>().pos = hook + Math::Vec3(0.0f, 0.0f, 0.3f);
    test.Run(0.5);

    const JointState state = World(test).GetJointState(joint);
    INFO("the box slid 0.3 m along world Z, frameA's X, from the rail (bodyA); it reads " << state.position);
    REQUIRE(state.position == Approx(0.3f).margin(2e-3));
    INFO("frameB sits 0.3 m along frameA's X, unturned; it is at "
         << state.relative.position.x << ", " << state.relative.position.y << ", " << state.relative.position.z);
    REQUIRE(state.relative.position.x == Approx(0.3f).margin(2e-3));
    REQUIRE(state.relative.position.y == Approx(0.0f).margin(2e-3));
    REQUIRE(state.relative.position.z == Approx(0.0f).margin(2e-3));
    REQUIRE(std::abs(state.relative.rotation.w) == Approx(1.0f).margin(1e-4));
}

TEST_CASE("A distance joint reads its length", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    Entity weight = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity rope = test.Entities().CreateEntity();
    // A rope 2 m long from the hook to the middle of the weight's top face, made slack: the weight falls until taut.
    Join(rope, weight, Entity(), JointFrame{hook}, DistanceJoint{.minDistance = 0.0f, .maxDistance = 2.0f});
    test.Run(3.0);

    const float length = World(test).GetJointState(rope).position;
    INFO("the weight hangs on the taut rope, 2 m below the hook; the rope reads " << length);
    REQUIRE(length == Approx(2.0f).margin(5e-3));
}

namespace
{
    /** A load reading expected, and the joint holding it. */
    struct SteadyLoad
    {
        const char* what;
        JointKind kind;
        /** Where the unit box's centre is, from the hook: the box is joined to the world at the hook. */
        Math::Vec3 offset;
        /** On the world, bodyB: the box's weight, and its moment about the hook once it rests. */
        Math::Vec3 force;
        Math::Vec3 torque;
    };
} // namespace

TEST_CASE("Each kind of joint reads its steady load on bodyB", "[physics][joints][joint-state]")
{
    const float diagonal = std::numbers::sqrt2_v<float> / 2.0f;
    const Math::Vec3 weight(0.0f, -g, 0.0f);
    // A unit box held out level by its left face's middle: its weight turns it about world -Z by half a metre's lever.
    const Math::Vec3 levelArm(0.5f, 0.0f, 0.0f);
    const Math::Vec3 levelMoment(0.0f, 0.0f, -0.5f * g);
    const Math::Quat upright = Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
    const SteadyLoad loads[] = {
        {"a fixed joint holds the box out level", FixedJoint{}, levelArm, weight, levelMoment},
        {"a ball joint hangs the box", BallJoint{}, Math::Vec3(0.0f, -0.5f, 0.0f), weight, Math::Vec3(0.0f)},
        {"a rope hangs the box", DistanceJoint{.minDistance = 0.0f, .maxDistance = 0.5f}, Math::Vec3(0.0f, -0.5f, 0.0f),
         weight, Math::Vec3(0.0f)},
        {"a hinge about world up holds the box out level", HingeJoint{}, levelArm, weight, levelMoment},
        {"a level slider holds the box out level", SliderJoint{}, levelArm, weight, levelMoment},
        {"a swing-twist joint with no swing holds the box out level",
         SwingTwistJoint{.swingYLimit = 0.0f, .swingZLimit = 0.0f}, levelArm, weight, levelMoment},
        {"a swing-twist joint's cone lets the box sag to its 45 degree edge", SwingTwistJoint{}, levelArm, weight,
         Math::Vec3(0.0f, 0.0f, -0.5f * g * diagonal)},
        {"a swing-twist joint's twist limit lets the box roll to 45 degrees",
         SwingTwistJoint{.swingYLimit = 0.0f, .swingZLimit = 0.0f}, Math::Vec3(0.0f, 0.0f, 0.5f), weight,
         Math::Vec3(0.5f * g * diagonal, 0.0f, 0.0f)},
    };
    for (const SteadyLoad& load : loads)
    {
        PhysicsApp test;
        Entity box = AddBody(test.Entities(), hook + load.offset);
        Entity joint = test.Entities().CreateEntity();
        // The hinge's axis is world up, so the box's weight bends the hinge rather than turning it.
        const Math::Quat axes = load.kind.Is<HingeJoint>() ? upright : identity;
        Join(joint, box, Entity(), JointFrame{hook, axes}, load.kind);
        test.Run(4.0);

        const JointState state = World(test).GetJointState(joint);
        INFO(load.what << ": the force on the world is " << state.force.x << ", " << state.force.y << ", "
                       << state.force.z << ", expected " << load.force.x << ", " << load.force.y << ", "
                       << load.force.z);
        REQUIRE(Math::Magnitude(state.force - load.force) < 0.05f);
        INFO("the torque on the world is " << state.torque.x << ", " << state.torque.y << ", " << state.torque.z
                                           << ", expected " << load.torque.x << ", " << load.torque.y << ", "
                                           << load.torque.z);
        REQUIRE(Math::Magnitude(state.torque - load.torque) < 0.05f);
    }
}

namespace
{
    bool IsZeroed(const JointState& state)
    {
        return state.force == Math::Vec3(0.0f) && state.torque == Math::Vec3(0.0f) &&
               state.relative.position == Math::Vec3(0.0f) && state.relative.rotation == identity &&
               state.position == 0.0f;
    }
} // namespace

TEST_CASE("An entity with no Joint reads a zeroed joint state", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 1);
    test.Run(0.5);

    INFO("a body is not a joint");
    REQUIRE(IsZeroed(World(test).GetJointState(chain.links[0])));
    INFO("nor is the empty entity");
    REQUIRE(IsZeroed(World(test).GetJointState(Entity())));
    test.Entities().DestroyEntity(chain.joints[0]);
    INFO("nor is a joint entity destroyed since the last step");
    REQUIRE(IsZeroed(World(test).GetJointState(chain.joints[0])));
}

TEST_CASE("A joint reads a zeroed state until its first step", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 9.5f, 0.0f));
    test.Run(0.1);

    // Turned off its rest pose, so a stepped joint would read a pose.
    Entity joint = test.Entities().CreateEntity();
    Join(joint, box, Entity(), JointFrame{hook}, HingeJoint{}).frameB.position.z += 0.5f;
    INFO("a joint made since the last step reads zeroed");
    REQUIRE(IsZeroed(World(test).GetJointState(joint)));

    test.app.RunFrame(tick);
    INFO("after its first step it reads the box's weight");
    REQUIRE(World(test).GetJointState(joint).force.y < -1.0f);
}

TEST_CASE("A sleeping joint keeps the load it had awake", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 2);
    test.Run(3.0);
    REQUIRE(World(test).IsSleeping(chain.links[0]));
    test.Run(1.0);

    const JointState state = World(test).GetJointState(chain.joints[0]);
    INFO("the sleeping chain's top joint still pulls the world down with its two links' weight, 19.6 N; it reads "
         << state.force.y);
    REQUIRE(state.force.y == Approx(-2.0f * g).margin(0.05));
    INFO("and its pose reads the rest pose the chain sleeps in");
    REQUIRE(Math::Magnitude(state.relative.position) < 1e-3f);
}

TEST_CASE("A disabled joint reads no load, and its pose as its bodies move", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 1);
    test.Run(0.5);
    REQUIRE(World(test).GetJointState(chain.joints[0]).force.y < -1.0f);

    chain.joints[0].GetComponent<Joint>().enabled = false;
    test.Run(0.5);

    const JointState state = World(test).GetJointState(chain.joints[0]);
    INFO("the disabled joint carries nothing; it reads " << state.force.x << ", " << state.force.y << ", "
                                                         << state.force.z);
    REQUIRE(state.force == Math::Vec3(0.0f));
    REQUIRE(state.torque == Math::Vec3(0.0f));
    // The link fell freely for half a second: g t^2 / 2 below its hook, which sits that far above frameA.
    const float fallen = 0.5f * g * 0.25f;
    INFO("its pose follows the falling link: the hook sits about " << fallen << " m above the link's frame; it is at "
                                                                   << state.relative.position.y);
    REQUIRE(state.relative.position.y == Approx(fallen).margin(0.05));
}

TEST_CASE("An inactive joint reads a zeroed state", "[physics][joints][joint-state]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity notABody = test.Entities().CreateEntity();
    Entity joint = test.Entities().CreateEntity();
    Join(joint, box, notABody, JointFrame{hook}, BallJoint{});
    test.Run(0.5);

    INFO("a joint naming an entity that is not a body joins nothing, and reads zeroed");
    REQUIRE(IsZeroed(World(test).GetJointState(joint)));
}
