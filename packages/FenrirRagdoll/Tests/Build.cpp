// BuildRagdoll and DestroyRagdoll: the body and joint entities spawned under one parent, kinematic and grouped, the
// Ragdoll on the skeleton entity, what is refused, and what DestroyRagdoll removes.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirRagdoll/Builder.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "RagdollTest.hpp"

using Fenrir::Entity;
using Fenrir::RagdollDescription;
using Fenrir::SpawnedRagdoll;
using Fenrir::Math::Vec3;
using RagdollTest::Near;
using RagdollTest::Text;

namespace
{
    struct Built
    {
        RagdollTest::RagdollApp test;
        Entity skeleton;
        RagdollDescription description;
        SpawnedRagdoll spawned;

        explicit Built(Vec3 at = Vec3(0.0f))
        {
            // First, so the skeleton entity's id is not 0 (see the id 0 case below).
            test.AddGround();
            skeleton = test.AddSkeleton(RagdollTest::Humanoid());
            skeleton.GetComponent<Fenrir::Transform>().pos = at;
            description = Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap());
            spawned = Fenrir::BuildRagdoll(test.app, test.level, skeleton, description);
        }
    };

    // A skeleton and a good description, not yet built, for the refusals.
    struct Unbuilt
    {
        RagdollTest::RagdollApp test;
        Entity skeleton;
        RagdollDescription good;

        Unbuilt()
        {
            test.AddGround();
            skeleton = test.AddSkeleton(RagdollTest::Humanoid());
            good = Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap());
        }
    };

    std::size_t CountEntities(Fenrir::EntityList& entities)
    {
        std::size_t count = 0;
        const Fenrir::ComponentType types[] = {Fenrir::ComponentType::Native<Fenrir::Transform>()};
        entities.ForEachWith(types, [&](std::uint32_t) { ++count; });
        return count;
    }

    Vec3 WorldPoint(const Entity& entity, const Vec3& local)
    {
        return Vec3(Fenrir::GetWorldMatrix(entity) * Fenrir::Math::Vec4(local, 1.0f));
    }
} // namespace

TEST_CASE("BuildRagdoll spawns one kinematic, grouped body entity per part under one parent", "[ragdoll][build]")
{
    Built built;
    const SpawnedRagdoll& spawned = built.spawned;

    INFO("it built: " << spawned.error);
    REQUIRE(spawned.error.empty());
    INFO("sixteen bodies under a parent at the top of the scene");
    REQUIRE(spawned.bodies.size() == 16);
    REQUIRE(spawned.parent.IsValid());
    REQUIRE_FALSE(spawned.parent.GetComponent<Fenrir::Relationship>().parent.IsValid());

    for (std::size_t i = 0; i < spawned.bodies.size(); ++i)
    {
        const Entity& body = spawned.bodies[i];
        const Fenrir::RagdollPart& part = built.description.parts[i];
        INFO("body " << i << " (" << Text(part.bone) << ") is under the parent");
        REQUIRE(body.GetComponent<Fenrir::Relationship>().parent == spawned.parent);
        INFO(Text(part.bone) << " is a kinematic rigid body with the part's mass and capsule");
        REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Kinematic);
        REQUIRE(Near(body.GetComponent<Fenrir::RigidBody>().mass, part.mass));
        REQUIRE(body.GetComponent<Fenrir::Collider>().shape.Is<Fenrir::CapsuleShape>());
        INFO(Text(part.bone) << " is in the skeleton entity's collision group");
        REQUIRE(body.GetComponent<Fenrir::CollisionGroup>().group == built.skeleton.GetId());
        INFO(Text(part.bone) << " names the skeleton entity, its bone and offset, inheriting the ragdoll's mode");
        const Fenrir::RagdollBody& mapping = body.GetComponent<Fenrir::RagdollBody>();
        REQUIRE(mapping.ragdoll == built.skeleton);
        REQUIRE(Text(mapping.bone) == Text(part.bone));
        REQUIRE(Near(mapping.offset.pos, part.offset.pos));
        REQUIRE(mapping.mode == Fenrir::RagdollBodyMode::Inherit);
    }
}

TEST_CASE("BuildRagdoll puts a following Ragdoll on the skeleton entity, its root body the hips'", "[ragdoll][build]")
{
    Built built;
    REQUIRE(built.spawned.error.empty());
    INFO("the skeleton entity has a Ragdoll in Following mode, at full strength, in its own group");
    REQUIRE(built.skeleton.HasComponent<Fenrir::Ragdoll>());
    const Fenrir::Ragdoll& ragdoll = built.skeleton.GetComponent<Fenrir::Ragdoll>();
    REQUIRE(ragdoll.mode == Fenrir::RagdollMode::Following);
    REQUIRE(Near(ragdoll.strength, 1.0f));
    REQUIRE(ragdoll.group == built.skeleton.GetId());
    INFO("its root body is the first spawned, the pelvis's");
    REQUIRE(ragdoll.rootBody == built.spawned.bodies.front());
    REQUIRE(Text(ragdoll.rootBody.GetComponent<Fenrir::RagdollBody>().bone) == "pelvis");
}

TEST_CASE("BuildRagdoll joins each part to its parent's body, root first, with friction and no self-collision",
          "[ragdoll][build]")
{
    Built built(Vec3(3.0f, 0.0f, -2.0f));
    const SpawnedRagdoll& spawned = built.spawned;
    REQUIRE(spawned.error.empty());
    INFO("fifteen joints, one per part but the root");
    REQUIRE(spawned.joints.size() == 15);

    for (std::size_t i = 0; i < spawned.joints.size(); ++i)
    {
        const Fenrir::RagdollPart& part = built.description.parts[i + 1];
        const Entity& entity = spawned.joints[i];
        INFO("joint " << i << " joins " << Text(part.parent) << " to " << Text(part.bone) << ", under the parent");
        REQUIRE(entity.GetComponent<Fenrir::Relationship>().parent == spawned.parent);
        const Fenrir::Joint& joint = entity.GetComponent<Fenrir::Joint>();
        REQUIRE(joint.bodyA == RagdollTest::BodyOf(spawned, Text(part.parent)));
        REQUIRE(joint.bodyB == spawned.bodies[i + 1]);
        INFO("its parent's body was spawned before its own: joints come root first");
        std::size_t parentIndex = 0;
        while (spawned.bodies[parentIndex] != joint.bodyA)
        {
            ++parentIndex;
        }
        REQUIRE(parentIndex < i + 1);
        INFO("the two bodies never collide, and the joint is the part's kind with friction 0.5 and no motor");
        REQUIRE_FALSE(joint.collideConnected);
        REQUIRE(joint.kind.Type() == part.joint.Type());
        if (joint.kind.Is<Fenrir::HingeJoint>())
        {
            REQUIRE(Near(joint.kind.As<Fenrir::HingeJoint>().friction, 0.5f));
            REQUIRE(joint.kind.As<Fenrir::HingeJoint>().motor.mode == Fenrir::JointMotorMode::Off);
        }
        else
        {
            REQUIRE(Near(joint.kind.As<Fenrir::SwingTwistJoint>().friction, 0.5f));
            REQUIRE(joint.kind.As<Fenrir::SwingTwistJoint>().motor.mode == Fenrir::JointMotorMode::Off);
        }
        INFO("both frames sit at the part's anchor in the world: the skeleton entity's place + the model anchor");
        const Vec3 anchor = Vec3(3.0f, 0.0f, -2.0f) + part.jointAnchor;
        REQUIRE(Near(WorldPoint(joint.bodyA, joint.frameA.position), anchor, 1e-4f));
        REQUIRE(Near(WorldPoint(joint.bodyB, joint.frameB.position), anchor, 1e-4f));
    }
}

TEST_CASE("BuildRagdoll places each body on its bone at once", "[ragdoll][build]")
{
    Built built(Vec3(3.0f, 0.0f, -2.0f));
    REQUIRE(built.spawned.error.empty());
    for (const Entity& body : built.spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " is at its bone's rest pose × offset");
        const Fenrir::Transform expected = RagdollTest::OnBone(built.test.level, body);
        REQUIRE(Near(Fenrir::GetWorldTransform(body).pos, expected.pos, 1e-4f));
        REQUIRE(Near(Fenrir::GetWorldTransform(body).rot, expected.rot, 1e-4f));
    }
}

TEST_CASE("A description's group, strength and joint friction are the built ragdoll's", "[ragdoll][build]")
{
    Unbuilt unbuilt;
    RagdollDescription description = unbuilt.good;
    description.group = 77;
    description.strength = 0.25f;
    description.jointFriction = 2.0f;
    const SpawnedRagdoll spawned =
        Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, unbuilt.skeleton, description);
    REQUIRE(spawned.error.empty());
    INFO("every body, and the Ragdoll, take group 77, the ragdoll strength 0.25, the joints friction 2");
    for (const Entity& body : spawned.bodies)
    {
        REQUIRE(body.GetComponent<Fenrir::CollisionGroup>().group == 77);
    }
    REQUIRE(unbuilt.skeleton.GetComponent<Fenrir::Ragdoll>().group == 77);
    REQUIRE(Near(unbuilt.skeleton.GetComponent<Fenrir::Ragdoll>().strength, 0.25f));
    REQUIRE(
        Near(spawned.joints.front().GetComponent<Fenrir::Joint>().kind.As<Fenrir::SwingTwistJoint>().friction, 2.0f));
}

TEST_CASE("A skeleton entity whose id is 0, which is no group, gives its bodies the group Entity::Null",
          "[ragdoll][build]")
{
    RagdollTest::RagdollApp test;
    const Entity skeleton = test.AddSkeleton(RagdollTest::Humanoid());
    REQUIRE(skeleton.GetId() == 0);
    const SpawnedRagdoll spawned = Fenrir::BuildRagdoll(
        test.app, test.level, skeleton, Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap()));
    REQUIRE(spawned.error.empty());
    INFO("the bodies still share a group, one no live entity's id can be");
    for (const Entity& body : spawned.bodies)
    {
        REQUIRE(body.GetComponent<Fenrir::CollisionGroup>().group == Entity::Null);
    }
    REQUIRE(skeleton.GetComponent<Fenrir::Ragdoll>().group == Entity::Null);
}

TEST_CASE("BuildRagdoll spawns nothing from a description DescribeHumanoid refused", "[ragdoll][build]")
{
    Unbuilt unbuilt;
    Fenrir::HumanoidBones bones = RagdollTest::HumanoidMap();
    bones.head = RagdollTest::NameOf("Skull");
    const RagdollDescription refused = Fenrir::DescribeHumanoid(unbuilt.test.DataOf(unbuilt.skeleton), bones);
    REQUIRE_FALSE(refused.error.empty());
    const std::size_t before = CountEntities(unbuilt.test.Entities());

    const SpawnedRagdoll spawned =
        Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, unbuilt.skeleton, refused);
    INFO("an error, nothing spawned, no Ragdoll");
    REQUIRE_FALSE(spawned.error.empty());
    REQUIRE(spawned.bodies.empty());
    REQUIRE(CountEntities(unbuilt.test.Entities()) == before);
    REQUIRE_FALSE(unbuilt.skeleton.HasComponent<Fenrir::Ragdoll>());
}

TEST_CASE("BuildRagdoll spawns nothing for an entity without a ready SkeletonPose", "[ragdoll][build]")
{
    Unbuilt unbuilt;
    Entity plain = unbuilt.test.Entities().CreateEntity();
    const std::size_t before = CountEntities(unbuilt.test.Entities());
    const SpawnedRagdoll spawned = Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, plain, unbuilt.good);
    INFO("an error, nothing spawned, no Ragdoll");
    REQUIRE_FALSE(spawned.error.empty());
    REQUIRE(CountEntities(unbuilt.test.Entities()) == before);
    REQUIRE_FALSE(plain.HasComponent<Fenrir::Ragdoll>());
}

TEST_CASE("BuildRagdoll spawns nothing for a part naming a bone the skeleton lacks", "[ragdoll][build]")
{
    Unbuilt unbuilt;
    RagdollDescription renamed = unbuilt.good;
    renamed.parts[5].bone = RagdollTest::NameOf("elbow_l");
    const std::size_t before = CountEntities(unbuilt.test.Entities());
    const SpawnedRagdoll spawned =
        Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, unbuilt.skeleton, renamed);
    INFO("an error naming the bone, nothing spawned, no Ragdoll");
    REQUIRE(spawned.error.find("elbow_l") != std::string::npos);
    REQUIRE(CountEntities(unbuilt.test.Entities()) == before);
    REQUIRE_FALSE(unbuilt.skeleton.HasComponent<Fenrir::Ragdoll>());
}

TEST_CASE("BuildRagdoll spawns nothing for a part whose parent is not an earlier part", "[ragdoll][build]")
{
    Unbuilt unbuilt;
    RagdollDescription orphan = unbuilt.good;
    orphan.parts[1].parent = RagdollTest::NameOf("spine_03");
    const std::size_t before = CountEntities(unbuilt.test.Entities());
    const SpawnedRagdoll spawned = Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, unbuilt.skeleton, orphan);
    INFO("an error, nothing spawned, no Ragdoll");
    REQUIRE_FALSE(spawned.error.empty());
    REQUIRE(CountEntities(unbuilt.test.Entities()) == before);
    REQUIRE_FALSE(unbuilt.skeleton.HasComponent<Fenrir::Ragdoll>());
}

TEST_CASE("BuildRagdoll spawns nothing for a skeleton that already has a ragdoll", "[ragdoll][build]")
{
    Unbuilt unbuilt;
    REQUIRE(Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, unbuilt.skeleton, unbuilt.good).error.empty());
    const std::size_t before = CountEntities(unbuilt.test.Entities());
    const SpawnedRagdoll second =
        Fenrir::BuildRagdoll(unbuilt.test.app, unbuilt.test.level, unbuilt.skeleton, unbuilt.good);
    INFO("one ragdoll per skeleton: an error, nothing spawned");
    REQUIRE_FALSE(second.error.empty());
    REQUIRE(CountEntities(unbuilt.test.Entities()) == before);
}

TEST_CASE("DestroyRagdoll removes the parent, everything under it and the Ragdoll", "[ragdoll][build]")
{
    Built built;
    REQUIRE(built.spawned.error.empty());
    built.test.Run(2);
    Entity elsewhere = built.test.Entities().CreateEntity();

    Fenrir::DestroyRagdoll(built.test.level, built.skeleton);

    INFO("the parent, every body and every joint are gone");
    REQUIRE_FALSE(built.spawned.parent.IsValid());
    for (const Entity& body : built.spawned.bodies)
    {
        REQUIRE_FALSE(body.IsValid());
    }
    for (const Entity& joint : built.spawned.joints)
    {
        REQUIRE_FALSE(joint.IsValid());
    }
    INFO("the Ragdoll is removed; the skeleton and other entities stay");
    REQUIRE_FALSE(built.skeleton.HasComponent<Fenrir::Ragdoll>());
    REQUIRE(built.skeleton.IsValid());
    REQUIRE(elsewhere.IsValid());
    INFO("the frames after run with no warning");
    built.test.Run(2);
    REQUIRE(built.test.warnings->empty());
}

TEST_CASE("DestroyRagdoll clears the overrides on its bodies' bones and leaves the others", "[ragdoll][build]")
{
    Built built;
    REQUIRE(built.spawned.error.empty());
    const Fenrir::SkeletonData& data = built.test.DataOf(built.skeleton);
    const std::int32_t hips = data.FindBone("pelvis");
    const std::int32_t neck = data.FindBone("neck_01");
    REQUIRE(Fenrir::Skeletons::SetOverride(built.test.level, built.skeleton, hips, Fenrir::Target{}));
    REQUIRE(Fenrir::Skeletons::SetOverride(built.test.level, built.skeleton, neck, Fenrir::Target{}));

    Fenrir::DestroyRagdoll(built.test.level, built.skeleton);

    INFO("the pelvis, a ragdoll body's bone, loses its entry");
    REQUIRE_FALSE(Fenrir::Skeletons::GetOverride(built.test.level, built.skeleton, hips).has_value());
    INFO("the neck, which no body follows, keeps game code's entry");
    REQUIRE(Fenrir::Skeletons::GetOverride(built.test.level, built.skeleton, neck).has_value());
}
