// The ragdoll spike's headless checks (the physics map's ticket 55): a code-built humanoid skeleton, tick-posed, moved
// by an Animate stand-in for a walk clip, with a ragdoll derived and built through FenrirRagdoll's public interface.
// Following bodies land on the simulation bones with no lag; the switch to simulating keeps the move's velocity; the
// overrides written match the bodies; two runs are bit-identical.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdoll.hpp"
#include "FenrirRagdoll/RagdollDescription.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

using namespace Fenrir;
using Math::Quat;
using Math::Vec3;

namespace
{
    constexpr double Tick = 1.0 / 60.0;
    /** @brief The walk stand-in: the whole skeleton (its hips bone) moves along +X at this speed. */
    constexpr float WalkSpeed = 1.0f;

    class RecordingLogger final : public ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> messages) : m_messages(std::move(messages))
        {
        }
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view message) override
        {
            m_messages->emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            m_messages->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_messages->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_messages;
    };

    SkeletonBone Bone(std::string name, std::int32_t parent, Vec3 pos)
    {
        SkeletonBone bone;
        bone.sourceName = std::move(name);
        bone.parent = parent;
        bone.rest.pos = pos;
        return bone;
    }

    /** @brief A small humanoid, 1.8 m tall, standing on y = 0 facing +Z: hips, spine, chest, neck, head, two arms
     * hanging down (upper, lower, hand), two legs (thigh, calf, foot, toe). */
    SkeletonData Humanoid()
    {
        SkeletonData skeleton;
        auto& b = skeleton.bones;
        b.push_back(Bone("hips", NoBone, {0.0f, 1.0f, 0.0f}));    // 0
        b.push_back(Bone("spine", 0, {0.0f, 0.15f, 0.0f}));       // 1
        b.push_back(Bone("chest", 1, {0.0f, 0.2f, 0.0f}));        // 2
        b.push_back(Bone("neck", 2, {0.0f, 0.25f, 0.0f}));        // 3
        b.push_back(Bone("head", 3, {0.0f, 0.08f, 0.0f}));        // 4
        b.push_back(Bone("upperarm_l", 2, {0.2f, 0.2f, 0.0f}));   // 5
        b.push_back(Bone("lowerarm_l", 5, {0.0f, -0.28f, 0.0f})); // 6
        b.push_back(Bone("hand_l", 6, {0.0f, -0.26f, 0.0f}));     // 7
        b.push_back(Bone("upperarm_r", 2, {-0.2f, 0.2f, 0.0f}));  // 8
        b.push_back(Bone("lowerarm_r", 8, {0.0f, -0.28f, 0.0f})); // 9
        b.push_back(Bone("hand_r", 9, {0.0f, -0.26f, 0.0f}));     // 10
        b.push_back(Bone("thigh_l", 0, {0.1f, -0.05f, 0.0f}));    // 11
        b.push_back(Bone("calf_l", 11, {0.0f, -0.45f, 0.0f}));    // 12
        b.push_back(Bone("foot_l", 12, {0.0f, -0.45f, 0.0f}));    // 13
        b.push_back(Bone("toe_l", 13, {0.0f, -0.05f, 0.15f}));    // 14
        b.push_back(Bone("thigh_r", 0, {-0.1f, -0.05f, 0.0f}));   // 15
        b.push_back(Bone("calf_r", 15, {0.0f, -0.45f, 0.0f}));    // 16
        b.push_back(Bone("foot_r", 16, {0.0f, -0.45f, 0.0f}));    // 17
        b.push_back(Bone("toe_r", 17, {0.0f, -0.05f, 0.15f}));    // 18
        return skeleton;
    }

    HumanoidBones Map()
    {
        HumanoidBones bones;
        bones.hips = "hips";
        bones.spine = "spine";
        bones.chest = "chest";
        bones.head = "head";
        bones.leftUpperArm = "upperarm_l";
        bones.leftLowerArm = "lowerarm_l";
        bones.leftHand = "hand_l";
        bones.rightUpperArm = "upperarm_r";
        bones.rightLowerArm = "lowerarm_r";
        bones.rightHand = "hand_r";
        bones.leftUpperLeg = "thigh_l";
        bones.leftLowerLeg = "calf_l";
        bones.leftFoot = "foot_l";
        bones.rightUpperLeg = "thigh_r";
        bones.rightLowerLeg = "calf_r";
        bones.rightFoot = "foot_r";
        return bones;
    }

    /** @brief The app under test: assets, skeletons, physics (step poses only) and the ragdoll glue, one scene active,
     * with a ground and a ragdolled humanoid whose hips walk along +X in the tick pass. Members end in reverse. */
    struct RagdollApp
    {
        std::shared_ptr<std::vector<std::string>> messages = std::make_shared<std::vector<std::string>>();
        App app{std::make_unique<RecordingLogger>(messages), 1};
        Scene& level = app.CreateScene("Level");
        Assets assets = Assets::Install(app);
        Skeletons skeletons = Skeletons::Install(app);
        DebugDrawing debugDrawing = DebugDrawing::Install(app);
        Physics physics = Physics::Install(app, [] {
            PhysicsConfig config;
            config.interpolate = false;
            config.useWorkerThreads = false;
            return config;
        }());
        Ragdolls ragdolls = Ragdolls::Install(app);
        Entity skeleton;
        SpawnedRagdoll spawned;
        double clock = 0.0;

        RagdollApp()
        {
            app.ChangeActiveScene("Level");
            Entity ground = level.GetEntityList().CreateEntity();
            ground.GetComponent<Transform>().pos = Vec3(0.0f, -0.5f, 0.0f);
            ground.AddComponent<Collider>(Collider{BoxShape{Vec3(20.0f, 0.5f, 20.0f)}});

            const auto handle = assets.Store().Add<Skeleton>(Humanoid(), level);
            skeleton = level.GetEntityList().CreateEntity();
            skeleton.GetComponent<Name>().Set("Walker");
            skeleton.AddComponent<SkeletonPose>(SkeletonPose{handle, PoseTiming::Tick});
            skeletons.Ensure(level, skeleton);

            // The walk stand-in: the hips move along +X by the pass's step, on the tick clock.
            skeletons.AddAnimate("walk", 0, [this](AnimateContext& context) {
                for (const Entity& instance : context.Instances())
                {
                    if (instance != skeleton)
                    {
                        continue;
                    }
                    clock += context.Step();
                    Transform& hips = Skeletons::EditBasePose(context.GetScene(), instance)[0];
                    hips.pos = Vec3(static_cast<float>(clock) * WalkSpeed, 1.0f, 0.0f);
                }
            });

            const SkeletonData* data = assets.Store().TryGet(handle);
            REQUIRE(data != nullptr);
            const RagdollDescription description = DescribeHumanoid(*data, Map());
            REQUIRE(description.error.empty());
            spawned = BuildRagdoll(app, level, skeleton, description);
            REQUIRE(spawned.error.empty());
        }

        void Run(int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                app.RunFrame(Tick);
            }
        }

        Entity Body(std::string_view bone) const
        {
            for (const Entity& body : spawned.bodies)
            {
                if (body.GetComponent<RagdollBody>().bone.Get() == bone)
                {
                    return body;
                }
            }
            return {};
        }
    };

    bool Near(const Vec3& a, const Vec3& b, float tolerance)
    {
        return Math::Magnitude(a - b) < tolerance;
    }

    bool Near(const Quat& a, const Quat& b, float tolerance)
    {
        return std::abs(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) - 1.0f) < tolerance;
    }

    /** @brief `bone × offset` from the simulation pose: where a following body should be. */
    Transform Expected(RagdollApp& test, const Entity& body)
    {
        const RagdollBody& mapping = body.GetComponent<RagdollBody>();
        const std::int32_t bone = Skeletons::FindBone(test.level, test.skeleton, mapping.bone.Get().value_or(""));
        const Math::Mat4 world = *Skeletons::GetBoneWorldMatrix(test.level, test.skeleton, bone, PoseKind::Simulation);
        const Math::Mat4 bodyWorld = world * TransformToMat4(mapping.offset);
        Transform expected;
        Math::Decompose(bodyWorld, expected.pos, expected.rot, expected.scale);
        return expected;
    }
} // namespace

TEST_CASE("A humanoid is described with sixteen capsule parts, hinges at the elbows and knees", "[ragdoll-spike]")
{
    const SkeletonData data = [] {
        SkeletonData skeleton = Humanoid();
        return skeleton;
    }();
    // The store derives the names; the describer only needs FindBone, which works on the raw data too once added.
    RagdollApp test;
    const SkeletonData* stored = test.assets.Store().TryGet(test.skeleton.GetComponent<SkeletonPose>().skeleton);
    const RagdollDescription description = DescribeHumanoid(*stored, Map());

    // Ticket 53 said fifteen: hips, spine, chest and head, then three parts per limb, is sixteen.
    INFO("every slot filled gives sixteen parts, the hips first");
    REQUIRE(description.error.empty());
    REQUIRE(description.parts.size() == 16);
    REQUIRE(description.parts.front().bone.Get() == "hips");
    REQUIRE(description.parts.front().parent.Get() == "");
    std::size_t hinges = 0;
    std::size_t swingTwists = 0;
    float mass = 0.0f;
    for (const RagdollPart& part : description.parts)
    {
        REQUIRE(part.shape.Is<CapsuleShape>());
        mass += part.mass;
        if (part.parent.Get() == "")
        {
            continue;
        }
        hinges += part.joint.Is<HingeJoint>() ? 1 : 0;
        swingTwists += part.joint.Is<SwingTwistJoint>() ? 1 : 0;
    }
    INFO("four hinges (elbows and knees), eleven swing-twist joints, 70 kg in all");
    REQUIRE(hinges == 4);
    REQUIRE(swingTwists == 11);
    REQUIRE(std::abs(mass - 70.0f) < 0.01f);

    INFO("a name no bone has is refused");
    HumanoidBones wrong = Map();
    wrong.head = "skull";
    REQUIRE_FALSE(DescribeHumanoid(*stored, wrong).error.empty());
    INFO("an empty spine drops the chest, head and arms, leaving the hips and legs: seven parts");
    HumanoidBones noSpine = Map();
    noSpine.spine.clear();
    REQUIRE(DescribeHumanoid(*stored, noSpine).parts.size() == 7);
}

TEST_CASE("Following bodies sit on their simulation bones after every tick, with no lag", "[ragdoll-spike]")
{
    RagdollApp test;
    REQUIRE(test.spawned.bodies.size() == 16);
    INFO("the build warned about nothing");
    REQUIRE(test.messages->empty());

    for (int t = 0; t < 30; ++t)
    {
        test.Run(1);
        for (const Entity& body : test.spawned.bodies)
        {
            const Transform expected = Expected(test, body);
            const Transform actual = GetWorldTransform(body);
            INFO("tick " << t << ", " << body.GetComponent<RagdollBody>().bone.Get().value_or("") << ": expected ("
                         << expected.pos.x << ", " << expected.pos.y << ", " << expected.pos.z << ") got ("
                         << actual.pos.x << ", " << actual.pos.y << ", " << actual.pos.z << ")");
            REQUIRE(Near(actual.pos, expected.pos, 1e-3f));
            REQUIRE(Near(actual.rot, expected.rot, 1e-4f));
            REQUIRE(body.GetComponent<RigidBody>().motionType == MotionType::Kinematic);
        }
    }
    INFO("the hips have walked half a metre in thirty ticks: the body is on the moving pose, not a stale one");
    REQUIRE(std::abs(GetWorldTransform(test.Body("hips")).pos.x - 0.5f) < 0.05f);
    INFO("the backend's body is where the Transform says: its bounds are centred on it");
    const Entity chest = test.Body("chest");
    const std::optional<Math::AABB> bounds = PhysicsWorld::Of(test.level)->Bounds(chest);
    REQUIRE(bounds.has_value());
    const Vec3 centre = (bounds->min + bounds->max) * 0.5f;
    REQUIRE(Near(centre, GetWorldTransform(chest).pos, 0.02f));
    INFO("the ragdoll's state counts its bodies and is active");
    REQUIRE(test.skeleton.GetComponent<Ragdoll>().state.bodies == 16);
    REQUIRE_FALSE(test.skeleton.GetComponent<Ragdoll>().state.inactive);
}

TEST_CASE("Switching to simulating keeps the walk's velocity in the bodies", "[ragdoll-spike]")
{
    RagdollApp test;
    test.Run(10);
    test.skeleton.GetComponent<Ragdoll>().mode = RagdollMode::Simulating;
    test.Run(1);
    for (const Entity& body : test.spawned.bodies)
    {
        const RigidBody& rigid = body.GetComponent<RigidBody>();
        INFO(body.GetComponent<RagdollBody>().bone.Get().value_or("") << " is dynamic and moves at the walk's 1 m/s "
                                                                         "along x: got "
                                                                      << rigid.linearVelocity.x);
        REQUIRE(rigid.motionType == MotionType::Dynamic);
        REQUIRE(std::abs(rigid.linearVelocity.x - WalkSpeed) < 0.1f);
    }
}

TEST_CASE("While simulating, each body's bone gets a world override target at body × offset⁻¹", "[ragdoll-spike]")
{
    RagdollApp test;
    test.Run(5);
    test.skeleton.GetComponent<Ragdoll>().mode = RagdollMode::Simulating;
    test.Run(20);
    for (const Entity& body : test.spawned.bodies)
    {
        const RagdollBody& mapping = body.GetComponent<RagdollBody>();
        const std::int32_t bone = Skeletons::FindBone(test.level, test.skeleton, mapping.bone.Get().value_or(""));
        const std::optional<Target> target = Skeletons::GetOverride(test.level, test.skeleton, bone);
        INFO(mapping.bone.Get().value_or("") << " has a world target");
        REQUIRE(target.has_value());
        REQUIRE(target->space == PoseSpace::World);
        // body = bone × offset, so the target's bone pose × offset is the body's world transform.
        const Math::Mat4 boneWorld = TransformToMat4(Transform(target->trs.pos, target->trs.rot, Vec3(1.0f)));
        Transform fromTarget;
        Math::Decompose(boneWorld * TransformToMat4(mapping.offset), fromTarget.pos, fromTarget.rot, fromTarget.scale);
        const Transform actual = GetWorldTransform(body);
        REQUIRE(Near(fromTarget.pos, actual.pos, 1e-3f));
        REQUIRE(Near(fromTarget.rot, actual.rot, 1e-4f));
    }
    INFO("the skeleton entity followed the hips: it is no longer at the origin and stays upright");
    const Transform entity = GetWorldTransform(test.skeleton);
    REQUIRE(Math::Magnitude(entity.pos) > 0.01f);
    REQUIRE(std::abs(Math::Rotate(entity.rot, Vec3(0.0f, 1.0f, 0.0f)).y - 1.0f) < 1e-4f);
}

namespace
{
    /** @brief One run of the spike's sequence: follow, switch with a shove, a second of falling. Every body's world
     * transform at the end, as bytes. */
    std::vector<std::byte> RunSequence()
    {
        RagdollApp test;
        test.Run(10);
        test.skeleton.GetComponent<Ragdoll>().mode = RagdollMode::Simulating;
        PhysicsWorld::Of(test.level)->AddImpulse(test.Body("chest"), Vec3(0.0f, 0.0f, 120.0f));
        test.Run(60);
        std::vector<std::byte> bytes;
        for (const Entity& body : test.spawned.bodies)
        {
            const Transform world = GetWorldTransform(body);
            const auto* begin = reinterpret_cast<const std::byte*>(&world);
            bytes.insert(bytes.end(), begin, begin + sizeof(Transform));
        }
        return bytes;
    }
} // namespace

TEST_CASE("Two runs of the fall are bit-identical", "[ragdoll-spike]")
{
    const std::vector<std::byte> first = RunSequence();
    const std::vector<std::byte> second = RunSequence();
    INFO("every body's world transform after a second of falling matches byte for byte");
    REQUIRE(first.size() == second.size());
    REQUIRE(std::memcmp(first.data(), second.data(), first.size()) == 0);
}
