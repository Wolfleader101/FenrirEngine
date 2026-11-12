// Following (ADR-0040, ADR-0054): a ragdoll's kinematic bodies on their simulation bones after every tick, at one and
// at several ticks per frame, with velocities from consecutive poses, pushing what they walk through; the inactive
// rule for a frame-posed or unready skeleton and the skip rule for an unknown bone; a hand-written description.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "RagdollTest.hpp"

using Fenrir::Entity;
using Fenrir::SpawnedRagdoll;
using Fenrir::Math::Vec3;
using RagdollTest::Near;
using RagdollTest::Text;

namespace
{
    constexpr float WalkSpeed = 1.0f;

    // A humanoid walking along +X at WalkSpeed, its ragdoll built and following.
    struct Walker
    {
        RagdollTest::RagdollApp test;
        Entity skeleton;
        SpawnedRagdoll spawned;

        explicit Walker(const Fenrir::PhysicsConfig& config = RagdollTest::StepPosesOnly(),
                        Fenrir::PoseTiming timing = Fenrir::PoseTiming::Tick, std::size_t workerThreads = 1)
            : test(config, workerThreads)
        {
            test.AddGround();
            skeleton = test.AddSkeleton(RagdollTest::Humanoid(), timing);
            RagdollTest::AddWalk(test, WalkSpeed);
            spawned = Fenrir::BuildRagdoll(test.app, test.level, skeleton,
                                           Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap()));
            REQUIRE(spawned.error.empty());
        }
    };

    // Every check made after a step: how many, and the first that failed.
    struct StepChecks
    {
        int steps = 0;
        std::string failure;
    };

    // After every step, whether each following body is on its simulation bone × offset.
    std::shared_ptr<StepChecks> CheckOnBones(Walker& walker)
    {
        auto checks = std::make_shared<StepChecks>();
        RagdollTest::AfterEachStep(walker.test, [checks, &walker] {
            ++checks->steps;
            for (const Entity& body : walker.spawned.bodies)
            {
                const Fenrir::Transform expected = RagdollTest::OnBone(walker.test.level, body);
                const Fenrir::Transform actual = Fenrir::GetWorldTransform(body);
                if (checks->failure.empty() &&
                    (!Near(actual.pos, expected.pos, 1e-4f) || !Near(actual.rot, expected.rot, 1e-4f)))
                {
                    checks->failure = "step " + std::to_string(checks->steps) + ": " +
                                      Text(body.GetComponent<Fenrir::RagdollBody>().bone) + " is at (" +
                                      std::to_string(actual.pos.x) + ", " + std::to_string(actual.pos.y) + ", " +
                                      std::to_string(actual.pos.z) + "), its bone puts it at (" +
                                      std::to_string(expected.pos.x) + ", " + std::to_string(expected.pos.y) + ", " +
                                      std::to_string(expected.pos.z) + ")";
                }
            }
        });
        return checks;
    }
} // namespace

TEST_CASE("Following bodies are on their simulation bones after every tick, at one tick per frame", "[ragdoll][follow]")
{
    Walker walker;
    const auto checks = CheckOnBones(walker);
    walker.test.Run(30);

    INFO("thirty frames, thirty steps, every body on its bone after each: " << checks->failure);
    REQUIRE(checks->steps == 30);
    REQUIRE(checks->failure.empty());
    INFO("the hips have walked half a metre: the bodies are on the moving pose");
    REQUIRE(Near(Fenrir::GetWorldTransform(walker.spawned.bodies.front()).pos.x, 0.5f, 1e-3f));
    for (const Entity& body : walker.spawned.bodies)
    {
        INFO("each body stays kinematic");
        REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Kinematic);
    }
    INFO("nothing was warned about");
    REQUIRE(walker.test.warnings->empty());
}

TEST_CASE("Following bodies are on their simulation bones after every tick, at several ticks per frame",
          "[ragdoll][follow]")
{
    // Interpolation on, as in a game: between frames the bodies are shown blended, and each tick still sees them true.
    Fenrir::PhysicsConfig config = RagdollTest::StepPosesOnly();
    config.interpolate = true;
    Walker walker(config);
    const auto checks = CheckOnBones(walker);
    // 3.25 ticks a frame: three ticks, then four, then three, with a quarter tick left over each time.
    for (int frame = 0; frame < 8; ++frame)
    {
        walker.test.app.RunFrame(3.25 * RagdollTest::Tick);
    }

    INFO("twenty-six steps over eight frames, every body on its bone after each: " << checks->failure);
    REQUIRE(checks->steps == 26);
    REQUIRE(checks->failure.empty());
}

TEST_CASE("A body's first following tick has no earlier pose, so no velocity", "[ragdoll][follow]")
{
    Walker walker;
    walker.test.Run(1);
    for (const Entity& body : walker.spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " has no velocity after one tick");
        REQUIRE(Near(body.GetComponent<Fenrir::RigidBody>().linearVelocity, Vec3(0.0f)));
        REQUIRE(Near(body.GetComponent<Fenrir::RigidBody>().angularVelocity, Vec3(0.0f)));
    }
}

TEST_CASE("Following bodies' linear velocities are their pose's change over the tick", "[ragdoll][follow]")
{
    Walker walker;
    walker.test.Run(6);
    for (const Entity& body : walker.spawned.bodies)
    {
        const Fenrir::RigidBody& rigid = body.GetComponent<Fenrir::RigidBody>();
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone)
             << " moves with the walk, 1 m/s along +X, unturning: got (" << rigid.linearVelocity.x << ", "
             << rigid.linearVelocity.y << ", " << rigid.linearVelocity.z << ")");
        REQUIRE(Near(rigid.linearVelocity, Vec3(WalkSpeed, 0.0f, 0.0f), 1e-3f));
        REQUIRE(Near(rigid.angularVelocity, Vec3(0.0f), 1e-3f));
    }
}

TEST_CASE("Following bodies' angular velocities are their pose's turn over the tick", "[ragdoll][follow]")
{
    // A chain turning about +Y at 2 rad/s: every body turns with it.
    RagdollTest::RagdollApp spinning;
    const Entity chain = spinning.AddSkeleton(RagdollTest::Chain());
    RagdollTest::AddWalk(spinning, 0.0f, 2.0f);
    Fenrir::RagdollDescription description;
    Fenrir::RagdollPart root;
    root.bone = RagdollTest::NameOf("root");
    root.shape = Fenrir::CapsuleShape{.halfHeight = 0.3f, .radius = 0.1f};
    root.offset.pos = Vec3(0.2f, 0.5f, 0.0f);
    description.parts.push_back(root);
    const SpawnedRagdoll turned = Fenrir::BuildRagdoll(spinning.app, spinning.level, chain, description);
    REQUIRE(turned.error.empty());
    spinning.Run(4);
    const Fenrir::RigidBody& rigid = turned.bodies.front().GetComponent<Fenrir::RigidBody>();
    INFO("the root's body turns at 2 rad/s about +Y: got ("
         << rigid.angularVelocity.x << ", " << rigid.angularVelocity.y << ", " << rigid.angularVelocity.z << ")");
    REQUIRE(Near(rigid.angularVelocity, Vec3(0.0f, 2.0f, 0.0f), 1e-3f));
    // Its centre sits 0.2 from the turning axis, so it moves at 0.4 m/s.
    INFO("and moves at 0.2 m × 2 rad/s round the axis");
    REQUIRE(Near(Fenrir::Math::Magnitude(rigid.linearVelocity), 0.4f, 2e-3f));
}

TEST_CASE("A rig with a scaled armature puts its bodies on its bones, built and following", "[ragdoll][follow]")
{
    RagdollTest::RagdollApp test;
    test.AddGround();
    // An armature scale, as imported rigs often carry: the pelvis, and everything under it, at half size.
    Fenrir::SkeletonData rig = RagdollTest::Humanoid();
    rig.bones[0].rest.scale = Vec3(0.5f);
    const Entity skeleton = test.AddSkeleton(std::move(rig));
    RagdollTest::AddWalk(test, WalkSpeed);
    const SpawnedRagdoll spawned = Fenrir::BuildRagdoll(
        test.app, test.level, skeleton, Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap()));
    REQUIRE(spawned.error.empty());
    for (const Entity& body : spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " is built on its scaled bone × offset");
        REQUIRE(Near(Fenrir::GetWorldTransform(body).pos, RagdollTest::OnBone(test.level, body).pos, 1e-4f));
    }
    INFO("the calf's body sits half way between the half-size calf (0.05, 0.75, 0) and foot (0.05, 0.55, 0)");
    REQUIRE(
        Near(Fenrir::GetWorldTransform(RagdollTest::BodyOf(spawned, "calf_l")).pos, Vec3(0.05f, 0.65f, 0.0f), 1e-4f));
    test.Run(10);
    for (const Entity& body : spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " follows its scaled bone × offset");
        REQUIRE(Near(Fenrir::GetWorldTransform(body).pos, RagdollTest::OnBone(test.level, body).pos, 1e-4f));
        REQUIRE(Near(Fenrir::GetWorldTransform(body).rot, RagdollTest::OnBone(test.level, body).rot, 1e-4f));
    }
}

TEST_CASE("A game write to a following body's Transform is overwritten next tick", "[ragdoll][follow]")
{
    Walker walker;
    walker.test.Run(3);
    const Entity chest = RagdollTest::BodyOf(walker.spawned, "spine_03");
    chest.GetComponent<Fenrir::Transform>().pos = Vec3(50.0f, 50.0f, 50.0f);
    const auto checks = CheckOnBones(walker);
    walker.test.Run(1);

    INFO("the chest is back on its bone after the next step: " << checks->failure);
    REQUIRE(checks->steps == 1);
    REQUIRE(checks->failure.empty());
}

TEST_CASE("Following bodies push the dynamic bodies they walk through", "[ragdoll][follow]")
{
    Walker walker;
    // A 1 kg box in the legs' way, resting on the ground 0.7 m ahead of the leading leg's shin (at x = 0.1 + its
    // radius, the walk leading with the left side).
    Entity box = walker.test.Entities().CreateEntity();
    box.GetComponent<Fenrir::Transform>().pos = Vec3(1.0f, 0.15f, 0.0f);
    box.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 1.0f});
    box.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(0.15f)}});
    walker.test.Run(30);
    INFO("half a second in, the box has not been reached: it rests where it was");
    REQUIRE(Near(Fenrir::GetWorldTransform(box).pos.x, 1.0f, 0.02f));

    walker.test.Run(90);
    const float x = Fenrir::GetWorldTransform(box).pos.x;
    INFO("two seconds in, the hips at x = 2, the walking legs have pushed the box ahead of them: x = " << x);
    REQUIRE(x > 1.8f);
}

TEST_CASE("A ragdoll on a frame-posed skeleton is inactive, warning once, and active once tick-posed",
          "[ragdoll][follow]")
{
    Walker walker(RagdollTest::StepPosesOnly(), Fenrir::PoseTiming::Frame);
    const Entity hips = walker.spawned.bodies.front();
    const Vec3 placed = Fenrir::GetWorldTransform(hips).pos;
    walker.test.Run(10);

    const Fenrir::Ragdoll& ragdoll = walker.skeleton.GetComponent<Fenrir::Ragdoll>();
    INFO("ten frames on a frame-posed skeleton: inactive, with one warning naming PoseTiming::Tick");
    REQUIRE(ragdoll.state.inactive);
    REQUIRE(walker.test.Warnings("PoseTiming::Tick") == 1);
    REQUIRE(walker.test.warnings->size() == 1);
    INFO("and its bodies were not moved, though the frame-posed walk moved the skeleton");
    REQUIRE(Near(Fenrir::GetWorldTransform(hips).pos, placed));

    walker.skeleton.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Tick;
    const auto checks = CheckOnBones(walker);
    walker.test.Run(5);
    INFO("switched to PoseTiming::Tick: active, every body on its bone, no new warning: " << checks->failure);
    REQUIRE_FALSE(walker.skeleton.GetComponent<Fenrir::Ragdoll>().state.inactive);
    REQUIRE(checks->failure.empty());
    REQUIRE(walker.test.warnings->size() == 1);

    walker.skeleton.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Frame;
    walker.test.Run(5);
    INFO("frame-posed again: inactive again, warned once more");
    REQUIRE(walker.skeleton.GetComponent<Fenrir::Ragdoll>().state.inactive);
    REQUIRE(walker.test.Warnings("PoseTiming::Tick") == 2);
}

TEST_CASE("A ragdoll whose skeleton is not ready is inactive, warning once", "[ragdoll][follow]")
{
    Walker walker;
    walker.test.Run(2);
    walker.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = {};
    walker.test.Run(5);
    INFO("an empty skeleton handle: inactive, one warning");
    REQUIRE(walker.skeleton.GetComponent<Fenrir::Ragdoll>().state.inactive);
    REQUIRE(walker.test.Warnings("inactive") == 1);
}

TEST_CASE("A body naming a bone the skeleton lacks is skipped with one warning, the rest following",
          "[ragdoll][follow]")
{
    Walker walker;
    walker.test.Run(2);
    const Entity hand = RagdollTest::BodyOf(walker.spawned, "hand_l");
    hand.GetComponent<Fenrir::RagdollBody>().bone = RagdollTest::NameOf("hand_left");
    walker.test.Run(10);

    INFO("one warning naming the missing bone, however many ticks");
    REQUIRE(walker.test.Warnings("hand_left") == 1);
    INFO("the body's resolved bone is NoBone, and the ragdoll drives the other fifteen");
    REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone == Fenrir::NoBone);
    REQUIRE(walker.skeleton.GetComponent<Fenrir::Ragdoll>().state.bodies == 15);
    const Entity chest = RagdollTest::BodyOf(walker.spawned, "spine_03");
    REQUIRE(Near(Fenrir::GetWorldTransform(chest).pos, RagdollTest::OnBone(walker.test.level, chest).pos));

    hand.GetComponent<Fenrir::RagdollBody>().bone = RagdollTest::NameOf("hand_l");
    walker.test.Run(1);
    INFO("named right again, it follows again");
    REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone ==
            walker.test.DataOf(walker.skeleton).FindBone("hand_l"));
    REQUIRE(Near(Fenrir::GetWorldTransform(hand).pos, RagdollTest::OnBone(walker.test.level, hand).pos));
}

TEST_CASE("The glue reports each ragdoll's and body's state", "[ragdoll][follow]")
{
    Walker walker;
    walker.test.Run(3);
    const Fenrir::RagdollState& state = walker.skeleton.GetComponent<Fenrir::Ragdoll>().state;
    INFO("active, driving all sixteen bodies, awake while walking");
    REQUIRE_FALSE(state.inactive);
    REQUIRE(state.bodies == 16);
    REQUIRE(state.anyAwake);
    for (const Entity& body : walker.spawned.bodies)
    {
        const Fenrir::RagdollBody& mapping = body.GetComponent<Fenrir::RagdollBody>();
        INFO(Text(mapping.bone) << " reports its bone index and the Following mode it inherits");
        REQUIRE(mapping.state.bone == walker.test.DataOf(walker.skeleton).FindBone(Text(mapping.bone)));
        REQUIRE(mapping.state.mode == Fenrir::RagdollMode::Following);
    }

    walker.spawned.bodies[3].GetComponent<Fenrir::RagdollBody>().mode = Fenrir::RagdollBodyMode::Simulating;
    walker.test.Run(1);
    INFO("a body's own mode is reported as its effective mode");
    REQUIRE(walker.spawned.bodies[3].GetComponent<Fenrir::RagdollBody>().state.mode == Fenrir::RagdollMode::Simulating);
}

TEST_CASE("A hand-written two-part description builds and follows the chain skeleton", "[ragdoll][follow]")
{
    RagdollTest::RagdollApp test;
    test.AddGround();
    const Entity chain = test.AddSkeleton(RagdollTest::Chain());
    RagdollTest::AddWalk(test, 0.5f);

    // Two capsules, root (0, 1, 0) to mid (0, 2, 0) and mid to tip (0, 3, 0), joined at mid by a swing-twist joint
    // twisting about the chain (up).
    Fenrir::RagdollDescription description;
    Fenrir::RagdollPart root;
    root.bone = RagdollTest::NameOf("root");
    root.shape = Fenrir::CapsuleShape{.halfHeight = 0.4f, .radius = 0.1f};
    root.offset.pos = Vec3(0.0f, 0.5f, 0.0f);
    root.mass = 5.0f;
    Fenrir::RagdollPart mid = root;
    mid.bone = RagdollTest::NameOf("mid");
    mid.parent = RagdollTest::NameOf("root");
    mid.mass = 4.0f;
    mid.joint = Fenrir::SwingTwistJoint{};
    mid.jointAnchor = Vec3(0.0f, 2.0f, 0.0f);
    mid.jointFrame.rotation = Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    description.parts = {root, mid};

    const SpawnedRagdoll spawned = Fenrir::BuildRagdoll(test.app, test.level, chain, description);
    INFO("it builds: " << spawned.error);
    REQUIRE(spawned.error.empty());
    REQUIRE(spawned.bodies.size() == 2);
    REQUIRE(spawned.joints.size() == 1);
    const Fenrir::Joint& joint = spawned.joints.front().GetComponent<Fenrir::Joint>();
    INFO("the joint sits at mid's origin on both bodies, its X (the twist axis) up the chain");
    REQUIRE(Near(Vec3(Fenrir::GetWorldMatrix(joint.bodyA) * Fenrir::Math::Vec4(joint.frameA.position, 1.0f)),
                 Vec3(0.0f, 2.0f, 0.0f)));
    REQUIRE(Near(Fenrir::Math::Rotate(Fenrir::GetWorldTransform(joint.bodyA).rot * joint.frameA.rotation,
                                      Vec3(1.0f, 0.0f, 0.0f)),
                 Vec3(0.0f, 1.0f, 0.0f)));

    test.Run(20);
    for (const Entity& body : spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " follows its bone");
        REQUIRE(Near(Fenrir::GetWorldTransform(body).pos, RagdollTest::OnBone(test.level, body).pos, 1e-4f));
    }
    INFO("the root's body has walked with the chain: 20 ticks at 0.5 m/s");
    REQUIRE(Near(Fenrir::GetWorldTransform(spawned.bodies.front()).pos, Vec3(1.0f / 6.0f, 1.5f, 0.0f), 1e-4f));
    REQUIRE(chain.GetComponent<Fenrir::Ragdoll>().state.bodies == 2);
}
