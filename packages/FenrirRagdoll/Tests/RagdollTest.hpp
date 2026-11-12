#pragma once

// What the ragdoll tests share (the spec's seam 2): a headless App with assets, skeletons, physics and the ragdoll glue
// and no renderer, two skeletons built in code (a three-bone chain and a humanoid shaped like the Quaternius human,
// never the downloaded asset, ADR-0052), Animate stand-ins for clips, and pose reads to check bodies against.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace RagdollTest
{
    // Records warnings and errors, so a test can check what was reported and how often. A second ConsoleLogger in one
    // process throws, so the tests never use one.
    class RecordingLogger final : public Fenrir::ILogger
    {
      public:
        RecordingLogger(std::shared_ptr<std::vector<std::string>> warnings,
                        std::shared_ptr<std::vector<std::string>> errors)
            : m_warnings(std::move(warnings)), m_errors(std::move(errors))
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
            m_warnings->emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_warnings;
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    inline constexpr double Tick = 1.0 / 60.0;

    /** @brief Physics as the tests want it by default: step poses only, on the App's thread, so reads are exact. */
    inline Fenrir::PhysicsConfig StepPosesOnly()
    {
        Fenrir::PhysicsConfig config;
        config.interpolate = false;
        config.useWorkerThreads = false;
        return config;
    }

    /**
     * @brief The App under test: assets, skeletons, debug drawing, physics and the ragdoll glue, one scene ("Level")
     * active. Members end in reverse order, so the owners go before the App.
     */
    struct RagdollApp
    {
        explicit RagdollApp(const Fenrir::PhysicsConfig& config = StepPosesOnly(), std::size_t workerThreads = 1)
            : app(std::make_unique<RecordingLogger>(warnings, errors), workerThreads), level(app.CreateScene("Level")),
              assets(Fenrir::Assets::Install(app)), skeletons(Fenrir::Skeletons::Install(app)),
              debugDrawing(Fenrir::DebugDrawing::Install(app)), physics(Fenrir::Physics::Install(app, config)),
              ragdolls(Fenrir::Ragdolls::Install(app))
        {
            app.ChangeActiveScene("Level");
        }

        std::shared_ptr<std::vector<std::string>> warnings = std::make_shared<std::vector<std::string>>();
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app;
        Fenrir::Scene& level;
        Fenrir::Assets assets;
        Fenrir::Skeletons skeletons;
        Fenrir::DebugDrawing debugDrawing;
        Fenrir::Physics physics;
        Fenrir::Ragdolls ragdolls;

        Fenrir::EntityList& Entities()
        {
            return level.GetEntityList();
        }

        /** @brief Runs `frames` frames of `delta` seconds each. */
        void Run(int frames, double delta = Tick)
        {
            for (int i = 0; i < frames; ++i)
            {
                app.RunFrame(delta);
            }
        }

        /** @brief How many warnings logged so far contain `text`. */
        [[nodiscard]] std::size_t Warnings(std::string_view text) const
        {
            std::size_t found = 0;
            for (const std::string& message : *warnings)
            {
                found += message.find(text) != std::string::npos ? 1 : 0;
            }
            return found;
        }

        /** @brief A skeleton entity posing `data`, tick-posed unless told otherwise, made `Ready` at once. */
        Fenrir::Entity AddSkeleton(Fenrir::SkeletonData data, Fenrir::PoseTiming timing = Fenrir::PoseTiming::Tick)
        {
            const auto handle = assets.Store().Add<Fenrir::Skeleton>(std::move(data), level);
            Fenrir::Entity skeleton = Entities().CreateEntity();
            skeleton.GetComponent<Fenrir::Name>().Set("Walker");
            skeleton.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{handle, timing});
            skeletons.Ensure(level, skeleton);
            return skeleton;
        }

        /** @brief The skeleton asset `skeleton` poses, as the store holds it (with its engine names derived). */
        const Fenrir::SkeletonData& DataOf(const Fenrir::Entity& skeleton)
        {
            return *assets.Store().TryGet(skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton);
        }

        /** @brief A static box whose top face is at y = 0, 40 wide and deep. */
        Fenrir::Entity AddGround()
        {
            Fenrir::Entity ground = Entities().CreateEntity();
            ground.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(0.0f, -0.5f, 0.0f);
            ground.AddComponent<Fenrir::Collider>(
                Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(20.0f, 0.5f, 20.0f)}});
            return ground;
        }
    };

    inline Fenrir::SkeletonBone Bone(std::string name, std::int32_t parent, Fenrir::Math::Vec3 pos)
    {
        Fenrir::SkeletonBone bone;
        bone.sourceName = std::move(name);
        bone.parent = parent;
        bone.rest.pos = pos;
        return bone;
    }

    /** @brief The three-bone chain: root at (0, 1, 0), then mid and tip each one unit above their parent. */
    inline Fenrir::SkeletonData Chain()
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("root", Fenrir::NoBone, {0.0f, 1.0f, 0.0f}), Bone("mid", 0, {0.0f, 1.0f, 0.0f}),
                          Bone("tip", 1, {0.0f, 1.0f, 0.0f})};
        return skeleton;
    }

    /**
     * @brief A humanoid shaped like the Quaternius human, built in code: Y up, facing +Z, its left side at +X, standing
     * on y = 0. 24 bones, with the Quaternius names: an unmapped spine_02 between the spine and chest, a neck, a
     * clavicle per side, a finger per hand and a ball per foot. Rest positions in model space:
     *
     * - pelvis (0, 1, 0); spine_01 (0, 1.1, 0); spine_02 (0, 1.25, 0); spine_03 (0, 1.4, 0); neck_01 (0, 1.6, 0);
     *   Head (0, 1.7, 0), with no child;
     * - clavicle_l (0.05, 1.55, 0); upperarm_l (0.2, 1.55, 0); lowerarm_l (0.2, 1.25, 0); hand_l (0.2, 1, 0);
     *   index_01_l (0.2, 0.9, 0); the right arm mirrored at -X;
     * - thigh_l (0.1, 0.95, 0); calf_l (0.1, 0.5, 0); foot_l (0.1, 0.1, 0); ball_l (0.1, 0.05, 0.15); the right leg
     *   mirrored.
     *
     * Its stature, Head to foot_l, is √(0.1² + 1.6²).
     */
    inline Fenrir::SkeletonData Humanoid()
    {
        using Fenrir::Math::Vec3;
        Fenrir::SkeletonData skeleton;
        auto& b = skeleton.bones;
        b.push_back(Bone("pelvis", Fenrir::NoBone, Vec3(0.0f, 1.0f, 0.0f))); // 0
        b.push_back(Bone("spine_01", 0, Vec3(0.0f, 0.1f, 0.0f)));            // 1
        b.push_back(Bone("spine_02", 1, Vec3(0.0f, 0.15f, 0.0f)));           // 2
        b.push_back(Bone("spine_03", 2, Vec3(0.0f, 0.15f, 0.0f)));           // 3
        b.push_back(Bone("neck_01", 3, Vec3(0.0f, 0.2f, 0.0f)));             // 4
        b.push_back(Bone("Head", 4, Vec3(0.0f, 0.1f, 0.0f)));                // 5
        for (const float side : {1.0f, -1.0f})
        {
            const std::string s = side > 0.0f ? "_l" : "_r";
            const auto clavicle = static_cast<std::int32_t>(b.size());
            b.push_back(Bone("clavicle" + s, 3, Vec3(0.05f * side, 0.15f, 0.0f)));
            b.push_back(Bone("upperarm" + s, clavicle, Vec3(0.15f * side, 0.0f, 0.0f)));
            b.push_back(Bone("lowerarm" + s, clavicle + 1, Vec3(0.0f, -0.3f, 0.0f)));
            b.push_back(Bone("hand" + s, clavicle + 2, Vec3(0.0f, -0.25f, 0.0f)));
            b.push_back(Bone("index_01" + s, clavicle + 3, Vec3(0.0f, -0.1f, 0.0f)));
        }
        for (const float side : {1.0f, -1.0f})
        {
            const std::string s = side > 0.0f ? "_l" : "_r";
            const auto thigh = static_cast<std::int32_t>(b.size());
            b.push_back(Bone("thigh" + s, 0, Vec3(0.1f * side, -0.05f, 0.0f)));
            b.push_back(Bone("calf" + s, thigh, Vec3(0.0f, -0.45f, 0.0f)));
            b.push_back(Bone("foot" + s, thigh + 1, Vec3(0.0f, -0.4f, 0.0f)));
            b.push_back(Bone("ball" + s, thigh + 2, Vec3(0.0f, -0.05f, 0.15f)));
        }
        return skeleton;
    }

    /** @brief Sets a BoneName from a name the tests know fits. */
    inline Fenrir::BoneName NameOf(std::string_view name)
    {
        RecordingLogger logger(std::make_shared<std::vector<std::string>>(),
                               std::make_shared<std::vector<std::string>>());
        Fenrir::BoneName bone;
        bone.Set(name, logger);
        return bone;
    }

    /** @brief The humanoid bone map for Humanoid(), every slot filled. */
    inline Fenrir::HumanoidBones HumanoidMap()
    {
        Fenrir::HumanoidBones bones;
        bones.hips = NameOf("pelvis");
        bones.spine = NameOf("spine_01");
        bones.chest = NameOf("spine_03");
        bones.head = NameOf("Head");
        bones.leftUpperArm = NameOf("upperarm_l");
        bones.leftLowerArm = NameOf("lowerarm_l");
        bones.leftHand = NameOf("hand_l");
        bones.rightUpperArm = NameOf("upperarm_r");
        bones.rightLowerArm = NameOf("lowerarm_r");
        bones.rightHand = NameOf("hand_r");
        bones.leftUpperLeg = NameOf("thigh_l");
        bones.leftLowerLeg = NameOf("calf_l");
        bones.leftFoot = NameOf("foot_l");
        bones.rightUpperLeg = NameOf("thigh_r");
        bones.rightLowerLeg = NameOf("calf_r");
        bones.rightFoot = NameOf("foot_r");
        return bones;
    }

    /** @brief A bone name as a string, empty when the buffer is malformed. */
    inline std::string Text(const Fenrir::BoneName& name)
    {
        return std::string(name.Get().value_or(""));
    }

    /**
     * @brief Moves every listed skeleton's root bone in the Animate stage, by the pass's step: its position along +X
     * at `speed` metres per second of its own clock from its rest position, and its rotation about +Y at `spin`
     * radians per second. Clocks are per entity, so each pose names the clock that made it.
     */
    inline Fenrir::PoseStageHandle AddWalk(RagdollApp& test, float speed, float spin = 0.0f)
    {
        auto clocks = std::make_shared<std::vector<std::pair<std::uint32_t, double>>>();
        return test.skeletons.AddAnimate("walk", 0, [clocks, speed, spin](Fenrir::AnimateContext& context) {
            for (const Fenrir::Entity& instance : context.Instances())
            {
                double* clock = nullptr;
                for (auto& [id, value] : *clocks)
                {
                    clock = id == instance.GetId() ? &value : clock;
                }
                if (clock == nullptr)
                {
                    clocks->emplace_back(instance.GetId(), 0.0);
                    clock = &clocks->back().second;
                }
                *clock += context.Step();
                Fenrir::Transform& root = Fenrir::Skeletons::EditBasePose(context.GetScene(), instance)[0];
                root.pos.x = static_cast<float>(*clock) * speed;
                root.rot =
                    Fenrir::Math::AngleAxis(static_cast<float>(*clock) * spin, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
            }
        });
    }

    inline bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::abs(a - b) <= tolerance;
    }

    inline bool Near(const Fenrir::Math::Vec3& a, const Fenrir::Math::Vec3& b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
    }

    /** @brief Rotations that turn vectors the same way: q and -q are one rotation. */
    inline bool Near(const Fenrir::Math::Quat& a, const Fenrir::Math::Quat& b, float tolerance = 1e-4f)
    {
        return Near(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 1.0f, tolerance);
    }

    /** @brief Where a following body belongs now: its bone's simulation-pose world matrix × its offset. */
    inline Fenrir::Transform OnBone(Fenrir::Scene& scene, const Fenrir::Entity& body)
    {
        const Fenrir::RagdollBody& mapping = body.GetComponent<Fenrir::RagdollBody>();
        const std::int32_t bone = Fenrir::Skeletons::FindBone(scene, mapping.ragdoll, Text(mapping.bone));
        const Fenrir::Math::Mat4 world =
            *Fenrir::Skeletons::GetBoneWorldMatrix(scene, mapping.ragdoll, bone, Fenrir::PoseKind::Simulation) *
            Fenrir::TransformToMat4(mapping.offset);
        Fenrir::Transform expected;
        Fenrir::Math::Decompose(world, expected.pos, expected.rot, expected.scale);
        return expected;
    }

    /** @brief Where a simulating body puts its bone, in world space: the body's world pose (no scale) × its offset⁻¹.
     */
    inline Fenrir::Transform BoneFromBody(const Fenrir::Entity& body)
    {
        const Fenrir::Transform world = Fenrir::GetWorldTransform(body);
        const Fenrir::Math::Mat4 matrix =
            Fenrir::TransformToMat4(Fenrir::Transform(world.pos, world.rot, Fenrir::Math::Vec3(1.0f))) *
            Fenrir::Math::Inverse(Fenrir::TransformToMat4(body.GetComponent<Fenrir::RagdollBody>().offset));
        Fenrir::Transform bone;
        Fenrir::Math::Decompose(matrix, bone.pos, bone.rot, bone.scale);
        return bone;
    }

    /** @brief The spawned body whose RagdollBody names `bone`. */
    inline Fenrir::Entity BodyOf(const Fenrir::SpawnedRagdoll& spawned, std::string_view bone)
    {
        for (const Fenrir::Entity& body : spawned.bodies)
        {
            if (Text(body.GetComponent<Fenrir::RagdollBody>().bone) == bone)
            {
                return body;
            }
        }
        return {};
    }

    /** @brief Runs `check` after every physics step from now on: in PostTick, after the step (physics's PostTick system
     * was added first), while the simulation pose is still the tick's. */
    inline void AfterEachStep(RagdollApp& test, std::function<void()> check)
    {
        test.app.AddSequentialSystems(Fenrir::SchedulePriority::PostTick,
                                      {[check = std::move(check)](Fenrir::App&) { check(); }});
    }
} // namespace RagdollTest
