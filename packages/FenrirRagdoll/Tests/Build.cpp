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

TEST_CASE("BuildRagdoll spawns kinematic, grouped bodies under one parent, and the Ragdoll on the skeleton entity",
          "[ragdoll][build]")
{
    Built built;
    const SpawnedRagdoll& spawned = built.spawned;

    INFO("it built: " << spawned.error);
    REQUIRE(spawned.error.empty());
    INFO("sixteen bodies and fifteen joints");
    REQUIRE(spawned.bodies.size() == 16);
    REQUIRE(spawned.joints.size() == 15);
    REQUIRE(spawned.parent.IsValid());
    INFO("the parent sits at the top of the scene");
    REQUIRE_FALSE(spawned.parent.GetComponent<Fenrir::Relationship>().parent.IsValid());

    for (std::size_t i = 0; i < spawned.bodies.size(); ++i)
    {
        const Entity& body = spawned.bodies[i];
        INFO("body " << i << " (" << Text(built.description.parts[i].bone) << ")");
        INFO("is under the parent");
        REQUIRE(body.GetComponent<Fenrir::Relationship>().parent == spawned.parent);
        INFO("is a kinematic rigid body with the part's mass and shape");
        REQUIRE(body.HasComponent<Fenrir::RigidBody>());
        REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Kinematic);
        REQUIRE(Near(body.GetComponent<Fenrir::RigidBody>().mass, built.description.parts[i].mass));
        REQUIRE(body.HasComponent<Fenrir::Collider>());
        REQUIRE(body.GetComponent<Fenrir::Collider>().shape.Is<Fenrir::CapsuleShape>());
        INFO("is in the skeleton entity's collision group");
        REQUIRE(body.HasComponent<Fenrir::CollisionGroup>());
        REQUIRE(body.GetComponent<Fenrir::CollisionGroup>().group == built.skeleton.GetId());
        INFO("names the skeleton entity, its bone and offset, inheriting the ragdoll's mode");
        const Fenrir::RagdollBody& mapping = body.GetComponent<Fenrir::RagdollBody>();
        REQUIRE(mapping.ragdoll == built.skeleton);
        REQUIRE(Text(mapping.bone) == Text(built.description.parts[i].bone));
        REQUIRE(Near(mapping.offset.pos, built.description.parts[i].offset.pos));
        REQUIRE(mapping.mode == Fenrir::RagdollBodyMode::Inherit);
    }

    INFO("the skeleton entity has a following Ragdoll, its root body the hips'");
    REQUIRE(built.skeleton.HasComponent<Fenrir::Ragdoll>());
    const Fenrir::Ragdoll& ragdoll = built.skeleton.GetComponent<Fenrir::Ragdoll>();
    REQUIRE(ragdoll.mode == Fenrir::RagdollMode::Following);
    REQUIRE(ragdoll.rootBody == spawned.bodies.front());
    REQUIRE(Text(ragdoll.rootBody.GetComponent<Fenrir::RagdollBody>().bone) == "pelvis");
    REQUIRE(ragdoll.group == built.skeleton.GetId());
    REQUIRE(Near(ragdoll.strength, 1.0f));
}

TEST_CASE("BuildRagdoll joins each part to its parent's body, root first, with friction and no self-collision",
          "[ragdoll][build]")
{
    Built built(Vec3(3.0f, 0.0f, -2.0f));
    const SpawnedRagdoll& spawned = built.spawned;
    REQUIRE(spawned.error.empty());

    for (std::size_t i = 0; i < spawned.joints.size(); ++i)
    {
        const Fenrir::RagdollPart& part = built.description.parts[i + 1];
        const Entity& entity = spawned.joints[i];
        INFO("joint " << i << " joins " << Text(part.parent) << " to " << Text(part.bone));
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

TEST_CASE("A description's own group is used when it has one", "[ragdoll][build]")
{
    RagdollTest::RagdollApp test;
    const Entity skeleton = test.AddSkeleton(RagdollTest::Humanoid());
    RagdollDescription description = Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap());
    description.group = 77;
    description.strength = 0.25f;
    description.jointFriction = 2.0f;
    const SpawnedRagdoll spawned = Fenrir::BuildRagdoll(test.app, test.level, skeleton, description);
    REQUIRE(spawned.error.empty());
    INFO("every body, and the Ragdoll, take group 77, the ragdoll strength 0.25, the joints friction 2");
    for (const Entity& body : spawned.bodies)
    {
        REQUIRE(body.GetComponent<Fenrir::CollisionGroup>().group == 77);
    }
    REQUIRE(skeleton.GetComponent<Fenrir::Ragdoll>().group == 77);
    REQUIRE(Near(skeleton.GetComponent<Fenrir::Ragdoll>().strength, 0.25f));
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

TEST_CASE("BuildRagdoll spawns nothing from a refused description, an unready skeleton or a bad part",
          "[ragdoll][build]")
{
    RagdollTest::RagdollApp test;
    const Entity skeleton = test.AddSkeleton(RagdollTest::Humanoid());
    Fenrir::HumanoidBones bones = RagdollTest::HumanoidMap();
    bones.head = RagdollTest::NameOf("Skull");
    const RagdollDescription refused = Fenrir::DescribeHumanoid(test.DataOf(skeleton), bones);
    REQUIRE_FALSE(refused.error.empty());
    const std::size_t before = CountEntities(test.Entities());

    const SpawnedRagdoll fromError = Fenrir::BuildRagdoll(test.app, test.level, skeleton, refused);
    INFO("a description with an error: an error, nothing spawned, no Ragdoll");
    REQUIRE_FALSE(fromError.error.empty());
    REQUIRE(fromError.bodies.empty());
    REQUIRE(CountEntities(test.Entities()) == before);
    REQUIRE_FALSE(skeleton.HasComponent<Fenrir::Ragdoll>());

    const RagdollDescription good = Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap());
    Entity plain = test.Entities().CreateEntity();
    const std::size_t withPlain = CountEntities(test.Entities());
    const SpawnedRagdoll fromPlain = Fenrir::BuildRagdoll(test.app, test.level, plain, good);
    INFO("an entity without a ready SkeletonPose: an error, nothing spawned");
    REQUIRE_FALSE(fromPlain.error.empty());
    REQUIRE(CountEntities(test.Entities()) == withPlain);
    REQUIRE_FALSE(plain.HasComponent<Fenrir::Ragdoll>());

    RagdollDescription renamed = good;
    renamed.parts[5].bone = RagdollTest::NameOf("elbow_l");
    const SpawnedRagdoll fromRenamed = Fenrir::BuildRagdoll(test.app, test.level, skeleton, renamed);
    INFO("a part naming a bone the skeleton lacks: an error naming it, nothing spawned");
    REQUIRE(fromRenamed.error.find("elbow_l") != std::string::npos);
    REQUIRE(CountEntities(test.Entities()) == withPlain);

    RagdollDescription orphan = good;
    orphan.parts[1].parent = RagdollTest::NameOf("spine_03");
    const SpawnedRagdoll fromOrphan = Fenrir::BuildRagdoll(test.app, test.level, skeleton, orphan);
    INFO("a part whose parent is not an earlier part: an error, nothing spawned");
    REQUIRE_FALSE(fromOrphan.error.empty());
    REQUIRE(CountEntities(test.Entities()) == withPlain);
    REQUIRE_FALSE(skeleton.HasComponent<Fenrir::Ragdoll>());

    REQUIRE(Fenrir::BuildRagdoll(test.app, test.level, skeleton, good).error.empty());
    const std::size_t withRagdoll = CountEntities(test.Entities());
    INFO("a second ragdoll on one skeleton: an error, nothing spawned");
    REQUIRE_FALSE(Fenrir::BuildRagdoll(test.app, test.level, skeleton, good).error.empty());
    REQUIRE(CountEntities(test.Entities()) == withRagdoll);
}

TEST_CASE("DestroyRagdoll removes the parent, everything under it, the overrides and the Ragdoll", "[ragdoll][build]")
{
    Built built;
    REQUIRE(built.spawned.error.empty());
    built.test.Run(2);
    Entity elsewhere = built.test.Entities().CreateEntity();
    const std::int32_t hips = built.test.DataOf(built.skeleton).FindBone("pelvis");
    REQUIRE(Fenrir::Skeletons::SetOverride(built.test.level, built.skeleton, hips, Fenrir::Target{}));

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
    INFO("the Ragdoll is removed and the skeleton's overrides cleared; the skeleton and other entities stay");
    REQUIRE_FALSE(built.skeleton.HasComponent<Fenrir::Ragdoll>());
    REQUIRE_FALSE(Fenrir::Skeletons::GetOverride(built.test.level, built.skeleton, hips).has_value());
    REQUIRE(built.skeleton.IsValid());
    REQUIRE(elsewhere.IsValid());
    INFO("the frames after run with no warning");
    built.test.Run(2);
    REQUIRE(built.test.warnings->empty());
}
