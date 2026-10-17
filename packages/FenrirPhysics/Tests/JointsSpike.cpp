// PROTOTYPE (joints spike, ticket 27): a headless check of the door and chain the DemoApp's spike scene shows. Never
// merged.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float linkHalf = 0.25f;
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);

    // `count` unit-mass links hanging straight down from `hook`, each joined to the one above by a ball joint on its
    // own joint entity; the top one to the world.
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
            const float top = hook.y - 2.0f * linkHalf * static_cast<float>(i);
            Entity link = AddBody(entities, Math::Vec3(hook.x, top - linkHalf, hook.z),
                                  BoxShape{Math::Vec3(0.1f, linkHalf, 0.1f)});
            Entity jointEntity = entities.CreateEntity();
            const Entity above = i == 0 ? Entity() : chain.links.back();
            const JointFrames frames = FrameFromWorld(link, above, JointFrame{Math::Vec3(hook.x, top, hook.z)});
            jointEntity.AddComponent<Joint>(Joint{
                .bodyA = link, .bodyB = above, .frameA = frames.frameA, .frameB = frames.frameB, .kind = BallJoint{}});
            chain.links.push_back(link);
            chain.joints.push_back(jointEntity);
        }
        return chain;
    }

    // A door 1 wide, 2 tall, hinged on its left edge to a static post, its hinge axis world Y (frame X turned onto Y).
    struct Door
    {
        Entity post;
        Entity door;
    };

    Door MakeDoor(EntityList& entities, const HingeJoint& hinge)
    {
        Door made;
        made.post = Place(entities, Math::Vec3(-0.6f, 1.0f, 0.0f));
        made.post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f, 1.0f, 0.1f)}});
        made.door = AddBody(entities, Math::Vec3(0.0f, 1.0f, 0.0f), BoxShape{Math::Vec3(0.5f, 1.0f, 0.05f)});
        const Math::Quat axisUp = Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
        const JointFrames frames =
            FrameFromWorld(made.post, made.door, JointFrame{Math::Vec3(-0.5f, 1.0f, 0.0f), axisUp});
        made.door.AddComponent<Joint>(Joint{
            .bodyA = made.post, .bodyB = made.door, .frameA = frames.frameA, .frameB = frames.frameB, .kind = hinge});
        return made;
    }
} // namespace

TEST_CASE("Spike: a chain hangs from a world point and carries its weight", "[physics][joints-spike]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    test.Run(5.0);

    const Transform top = GetWorldTransform(chain.links[0]);
    INFO("the top link's upper end stays at the hook; its centre is at " << top.pos.x << ", " << top.pos.y << ", "
                                                                         << top.pos.z);
    REQUIRE(top.pos.y == Approx(hook.y - linkHalf).margin(0.02));
    const Transform bottom = GetWorldTransform(chain.links.back());
    REQUIRE(bottom.pos.y == Approx(hook.y - 9.0f * linkHalf).margin(0.05));

    PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
    const JointState state = world.JointState(chain.joints[0]);
    INFO("the top joint pulls the world (bodyB) down by the chain's weight: " << state.force.y);
    REQUIRE(state.force.y == Approx(-5.0f * 9.81f).margin(2.0f));
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Spike: destroying a link mid-swing makes its two joints inactive and drops the rest",
          "[physics][joints-spike]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 6);
    test.Run(0.5);
    PhysicsWorld::Of(test.app.GetActiveScene())->AddImpulse(chain.links.back(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(0.3);

    test.Entities().DestroyEntity(chain.links[2]);
    test.Run(2.0);

    INFO("one warning for each of the two joints naming the destroyed link");
    REQUIRE(test.warnings.size() == 2);
    REQUIRE(test.errors.empty());
    INFO("the links below fell; the bottom one is at " << PositionOf(chain.links.back()).y);
    REQUIRE(PositionOf(chain.links.back()).y < hook.y - 10.0f);
    INFO("the links above still hang");
    REQUIRE(PositionOf(chain.links[1]).y == Approx(hook.y - 3.0f * linkHalf).margin(0.1));
    const JointState inactive = PhysicsWorld::Of(test.app.GetActiveScene())->JointState(chain.joints[3]);
    REQUIRE(inactive.force == Math::Vec3(0.0f));
}

TEST_CASE("Spike: a hinged door stays within its limits and its motor drives it", "[physics][joints-spike]")
{
    PhysicsApp test;
    HingeJoint hinge;
    hinge.limits = JointLimits{.enabled = true, .min = -1.0f, .max = 1.0f, .spring = {}};
    const Door door = MakeDoor(test.Entities(), hinge);
    test.Run(0.1);
    PhysicsWorld* physicsWorld = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(physicsWorld != nullptr);

    INFO("at rest the hinge angle is 0");
    REQUIRE(physicsWorld->JointState(door.door).position == Approx(0.0f).margin(0.01));

    Joint& joint = door.door.GetComponent<Joint>();
    joint.kind.As<HingeJoint>().motor = JointMotor{.mode = JointMotorMode::Velocity, .targetVelocity = 2.0f};
    test.Run(2.0);
    const float unlimited = physicsWorld->JointState(door.door).position;
    joint.kind.As<HingeJoint>().motor.maxForce = 50.0f;
    test.Run(1.0);
    INFO("the motor turned the door to its upper limit: " << physicsWorld->JointState(door.door).position
                                                          << "; with the default infinite maxForce it was at "
                                                          << unlimited);
    REQUIRE(physicsWorld->JointState(door.door).position == Approx(1.0f).margin(0.05));

    joint.kind.As<HingeJoint>().motor = JointMotor{.mode = JointMotorMode::Position, .targetPosition = -0.5f};
    test.Run(3.0);
    INFO("the position motor brought it to -0.5: " << physicsWorld->JointState(door.door).position);
    REQUIRE(physicsWorld->JointState(door.door).position == Approx(-0.5f).margin(0.05));
    const Transform post = GetWorldTransform(door.post);
    REQUIRE(post.pos.y == Approx(1.0f));
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Spike: a motor edit wakes a sleeping door", "[physics][joints-spike]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test.Entities(), HingeJoint{});
    test.Run(3.0);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world->IsSleeping(door.door));

    door.door.GetComponent<Joint>().kind.As<HingeJoint>().motor =
        JointMotor{.mode = JointMotorMode::Velocity, .targetVelocity = 1.0f};
    test.Run(0.5);
    INFO("the door turned: " << world->JointState(door.door).position);
    REQUIRE(world->JointState(door.door).position > 0.3f);
}

TEST_CASE("Spike: a joint over its break force is disabled and sends JointBroken", "[physics][joints-spike]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 3);
    test.Entities().GetEntity(chain.joints[1].GetId()).GetComponent<Joint>().breakForce = 10.0f; // two links: ~19.6 N
    EventReader<JointBroken> broken;
    int events = 0;
    for (int frame = 0; frame < 30; ++frame)
    {
        test.app.RunFrame(tick);
        for (const JointBroken& event : broken.Read(test.app))
        {
            REQUIRE(event.joint == chain.joints[1]);
            ++events;
        }
    }
    INFO("joint 1 carries two links, more than 10 N, and broke, once");
    REQUIRE(events == 1);
    REQUIRE_FALSE(chain.joints[1].GetComponent<Joint>().enabled);
    REQUIRE(PositionOf(chain.links[2]).y < hook.y - 2.0f); // fell from 8.875
    REQUIRE(chain.joints[0].GetComponent<Joint>().enabled);
}

namespace
{
    // A chain swinging beside a spare joint made before it (the lowest joint entity). At 1 s the spare is removed, or
    // only disabled; returns where the links are at 4 s.
    std::vector<Math::Vec3> SwingAfterSpare(bool remove)
    {
        PhysicsApp test;
        AddGround(test.Entities());
        Entity left = AddBody(test.Entities(), Math::Vec3(-5.0f, 0.5f, 0.0f));
        Entity right = AddBody(test.Entities(), Math::Vec3(-3.8f, 0.5f, 0.0f));
        Entity spare = test.Entities().CreateEntity();
        const JointFrames frames = FrameFromWorld(left, right, JointFrame{Math::Vec3(-4.4f, 0.5f, 0.0f)});
        spare.AddComponent<Joint>(Joint{
            .bodyA = left, .bodyB = right, .frameA = frames.frameA, .frameB = frames.frameB, .kind = BallJoint{}});
        const Chain chain = MakeChain(test.Entities(), 6);
        test.Run(0.2);
        PhysicsWorld::Of(test.app.GetActiveScene())->AddImpulse(chain.links.back(), Math::Vec3(4.0f, 0.0f, 2.0f));
        test.Run(0.8);
        if (remove)
        {
            test.Entities().DestroyEntity(spare);
        }
        else
        {
            spare.GetComponent<Joint>().enabled = false;
        }
        test.Run(3.0);
        std::vector<Math::Vec3> positions;
        for (const Entity& link : chain.links)
        {
            positions.push_back(PositionOf(link));
        }
        return positions;
    }
} // namespace

TEST_CASE("Spike: removing an unrelated joint leaves a chain's solve order, and its motion, unchanged",
          "[physics][joints-spike]")
{
    const std::vector<Math::Vec3> removed = SwingAfterSpare(true);
    const std::vector<Math::Vec3> disabled = SwingAfterSpare(false);
    bool same = true;
    for (std::size_t i = 0; i < removed.size(); ++i)
    {
        same = same && std::bit_cast<std::array<std::uint32_t, 3>>(removed[i]) ==
                           std::bit_cast<std::array<std::uint32_t, 3>>(disabled[i]);
    }
    WARN("bottom link after removing the spare: "
         << removed.back().x << ", " << removed.back().y << ", " << removed.back().z
         << "; after only disabling it: " << disabled.back().x << ", " << disabled.back().y << ", " << disabled.back().z
         << (same ? " (bit for bit the same)" : " (differ)"));
    if (std::getenv("FENRIR_JOINTS_SPIKE_NO_PRIORITY") == nullptr)
    {
        REQUIRE(same);
    }
}

TEST_CASE("Spike: how violent a single-body teleport of a hinged door is", "[physics][joints-spike]")
{
    for (const bool together : {false, true})
    {
        PhysicsApp test;
        const Door door = MakeDoor(test.Entities(), HingeJoint{});
        test.Run(1.0);
        PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
        door.door.GetComponent<Transform>().pos.x += 0.7f;
        if (together)
        {
            door.post.GetComponent<Transform>().pos.x += 0.7f;
        }
        float peakForce = 0.0f;
        float peakSpeed = 0.0f;
        int settled = -1;
        for (int step = 0; step < 120; ++step)
        {
            test.app.RunFrame(tick);
            const JointState state = world.JointState(door.door);
            peakForce = std::max(peakForce, Math::Magnitude(state.force));
            peakSpeed = std::max(peakSpeed, Math::Magnitude(door.door.GetComponent<RigidBody>().linearVelocity));
            if (settled < 0 && Math::Magnitude(state.relative.position) < 0.01f)
            {
                settled = step + 1;
            }
        }
        WARN((together ? "door and post teleported 0.7 together" : "door alone teleported 0.7")
             << ": peak joint force " << peakForce << " N (its weight is 9.8 N), peak door speed " << peakSpeed
             << " m/s, frames within 1 cm after " << settled << " steps");
    }
}
