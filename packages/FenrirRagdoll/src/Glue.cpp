// The ragdoll glue (ADR-0054): Tick applies each ragdoll's mode to motion types and motors and moves following bodies
// onto the simulation pose; Update follows the root body with the skeleton entity and writes simulating bodies' poses
// into the skeleton as world override targets.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdoll.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Frames.hpp"

namespace Fenrir
{
    namespace Detail
    {
        struct RagdollsState
        {
            explicit RagdollsState(App& app) : app(&app)
            {
            }

            App* app;
            /** @brief Each following body's last written world pose, by body entity id: the next move's velocity. */
            std::unordered_map<std::uint32_t, RestPose> lastFollowed;
            /** @brief Entities warned about as inactive ragdolls or unmapped bodies, so each is warned about once. */
            std::unordered_set<std::uint32_t> warned;
        };
    } // namespace Detail

    namespace
    {
        using Math::Quat;
        using Math::Vec3;

        /** @brief SPIKE: how strength becomes torque and springs. Torque per kilogram of the driven body at strength
         * 1, and the spring's frequency from strength 0 to 1 (damping ratio 1). */
        constexpr float TorquePerKilogram = 30.0f;
        constexpr float SpringHzAtZero = 1.0f;
        constexpr float SpringHzAtOne = 10.0f;

        RagdollMode EffectiveMode(const Ragdoll& ragdoll, const RagdollBody& body)
        {
            switch (body.mode)
            {
            case RagdollBodyMode::Following:
                return RagdollMode::Following;
            case RagdollBodyMode::Simulating:
                return RagdollMode::Simulating;
            case RagdollBodyMode::Powered:
                return RagdollMode::Powered;
            case RagdollBodyMode::Inherit:
                break;
            }
            return ragdoll.mode;
        }

        /** @brief Whether the ragdoll on `skeleton` can run: a ready, tick-posed skeleton. Warns once when not. */
        bool Active(Detail::RagdollsState& state, Scene& scene, const Entity& skeleton)
        {
            std::string why;
            if (!skeleton.HasComponent<SkeletonPose>())
            {
                why = "it has no SkeletonPose";
            }
            else if (skeleton.GetComponent<SkeletonPose>().timing != PoseTiming::Tick)
            {
                why = "its skeleton is frame-posed: set SkeletonPose::timing to PoseTiming::Tick";
            }
            else if (Skeletons::GetPoseState(scene, skeleton) != PoseState::Ready)
            {
                why = "its SkeletonPose is not Ready";
            }
            if (why.empty())
            {
                state.warned.erase(skeleton.GetId());
                return true;
            }
            if (state.warned.insert(skeleton.GetId()).second)
            {
                state.app->Logger()->Warn("The Ragdoll of entity " + std::to_string(skeleton.GetId()) +
                                          " is inactive: " + why);
            }
            return false;
        }

        struct Mapped
        {
            Entity entity;
            std::int32_t bone;
            RagdollMode mode;
        };

        /** @brief The bodies of `skeleton`'s ragdoll that have a RigidBody and a bone, each warned about once when not,
         * with their effective modes written to their RagdollBody state. */
        std::vector<Mapped> BodiesOf(Detail::RagdollsState& state, Scene& scene, const Entity& skeleton,
                                     const Ragdoll& ragdoll, const std::vector<Entity>& candidates)
        {
            std::vector<Mapped> mapped;
            for (const Entity& entity : candidates)
            {
                RagdollBody& body = entity.GetComponent<RagdollBody>();
                if (body.ragdoll != skeleton)
                {
                    continue;
                }
                std::string why;
                std::int32_t bone = NoBone;
                if (!entity.HasComponent<RigidBody>())
                {
                    why = "it has no RigidBody";
                }
                else
                {
                    bone = Skeletons::FindBone(scene, skeleton, body.bone.Get().value_or(""));
                    if (bone == NoBone)
                    {
                        why = "no bone is named '" + std::string(body.bone.Get().value_or("")) + "'";
                    }
                }
                body.state.mode = EffectiveMode(ragdoll, body);
                body.state.bone = bone;
                if (!why.empty())
                {
                    if (state.warned.insert(entity.GetId()).second)
                    {
                        state.app->Logger()->Warn("The RagdollBody of entity " + std::to_string(entity.GetId()) +
                                                  " is skipped: " + why);
                    }
                    continue;
                }
                state.warned.erase(entity.GetId());
                mapped.push_back(Mapped{entity, bone, body.state.mode});
            }
            return mapped;
        }

        std::vector<Entity> With(EntityList& entities, ComponentType type)
        {
            std::vector<Entity> found;
            const ComponentType types[] = {type};
            entities.ForEachWith(types, [&](std::uint32_t id) { found.push_back(entities.GetEntity(id)); });
            return found;
        }

        /** @brief The base-pose (simulation) rotation of a bone in model space. */
        std::optional<Quat> BaseRotation(Scene& scene, const Entity& skeleton, std::int32_t bone)
        {
            const std::optional<Math::Mat4> model =
                Skeletons::GetBaseModel(scene, skeleton, bone, PoseKind::Simulation);
            if (!model)
            {
                return std::nullopt;
            }
            return Detail::PoseOf(*model).rot;
        }

        /** @brief Writes the motor of the joint whose child body is `child`: Position towards the base pose while the
         * child is powered, else Off. */
        void DriveJoint(Scene& scene, const Entity& skeleton, const Ragdoll& ragdoll, Joint& joint, const Mapped& child)
        {
            const bool powered = child.mode == RagdollMode::Powered;
            const RagdollBody& childBody = child.entity.GetComponent<RagdollBody>();
            const float mass = child.entity.GetComponent<RigidBody>().mass;
            const float strength = std::clamp(ragdoll.strength * childBody.strengthScale, 0.0f, 1.0f);
            const float maxTorque = strength * TorquePerKilogram * mass;
            const JointSpring spring{.frequency = Math::Lerp(SpringHzAtZero, SpringHzAtOne, strength), .damping = 1.0f};

            // frameB relative to frameA in the base pose: (boneA × offsetA × frameA)⁻¹ × (boneB × offsetB × frameB).
            std::optional<Quat> relative;
            if (powered && joint.bodyA.IsValid() && joint.bodyA.HasComponent<RagdollBody>())
            {
                const RagdollBody& parentBody = joint.bodyA.GetComponent<RagdollBody>();
                const std::int32_t parentBone = parentBody.state.bone;
                const std::optional<Quat> qA =
                    parentBone == NoBone ? std::nullopt : BaseRotation(scene, skeleton, parentBone);
                const std::optional<Quat> qB = BaseRotation(scene, skeleton, child.bone);
                if (qA && qB)
                {
                    relative = Math::Normalized(Math::Conjugate(joint.frameA.rotation) *
                                                Math::Conjugate(parentBody.offset.rot) * Math::Conjugate(*qA) * *qB *
                                                childBody.offset.rot * joint.frameB.rotation);
                }
            }
            const JointMotorMode mode = powered && relative ? JointMotorMode::Position : JointMotorMode::Off;
            if (joint.kind.Is<SwingTwistJoint>())
            {
                SwingTwistMotor& motor = joint.kind.As<SwingTwistJoint>().motor;
                motor.mode = mode;
                motor.maxTorque = maxTorque;
                motor.spring = spring;
                if (relative)
                {
                    motor.targetRotation = *relative;
                }
            }
            else if (joint.kind.Is<HingeJoint>())
            {
                JointMotor& motor = joint.kind.As<HingeJoint>().motor;
                motor.mode = mode;
                motor.maxForce = maxTorque;
                motor.spring = spring;
                if (relative)
                {
                    motor.targetPosition = Detail::TwistAboutX(*relative);
                }
            }
        }

        void Tick(Detail::RagdollsState& state, App& app)
        {
            Scene& scene = app.GetActiveScene();
            EntityList& entities = scene.GetEntityList();
            const std::vector<Entity> ragdolls = With(entities, ComponentType::Native<Ragdoll>());
            if (ragdolls.empty())
            {
                return;
            }
            const std::vector<Entity> candidates = With(entities, ComponentType::Native<RagdollBody>());
            // Each ragdoll body's joint to its parent: the joint naming it as bodyB.
            std::unordered_map<std::uint32_t, Entity> jointOf;
            for (const Entity& joint : With(entities, ComponentType::Native<Joint>()))
            {
                const Entity& child = joint.GetComponent<Joint>().bodyB;
                if (child.IsValid() && child.HasComponent<RagdollBody>())
                {
                    jointOf[child.GetId()] = joint;
                }
            }
            const PhysicsWorld* world = PhysicsWorld::Of(scene);
            const float dt = static_cast<float>(app.GetTime().tickRate);

            for (const Entity& skeleton : ragdolls)
            {
                Ragdoll& ragdoll = skeleton.GetComponent<Ragdoll>();
                if (!Active(state, scene, skeleton))
                {
                    ragdoll.state = RagdollState{.inactive = true};
                    continue;
                }
                const std::vector<Mapped> bodies = BodiesOf(state, scene, skeleton, ragdoll, candidates);
                ragdoll.state.inactive = false;
                ragdoll.state.bodies = static_cast<std::uint32_t>(bodies.size());
                ragdoll.state.anyAwake = false;
                for (const Mapped& mapped : bodies)
                {
                    RagdollBody& body = mapped.entity.GetComponent<RagdollBody>();
                    RigidBody& rigid = mapped.entity.GetComponent<RigidBody>();
                    rigid.motionType =
                        mapped.mode == RagdollMode::Following ? MotionType::Kinematic : MotionType::Dynamic;
                    if (mapped.mode == RagdollMode::Following)
                    {
                        const std::optional<Math::Mat4> boneWorld =
                            Skeletons::GetBoneWorldMatrix(scene, skeleton, mapped.bone, PoseKind::Simulation);
                        if (boneWorld)
                        {
                            const Detail::RestPose pose = Detail::Compose(Detail::PoseOf(*boneWorld), body.offset);
                            SetWorldTransform(mapped.entity, Transform(pose.pos, pose.rot, Vec3(1.0f)));
                            // The move's velocity, kept in the RigidBody so the switch to dynamic starts from it:
                            // physics gives a body that becomes dynamic the velocity its component holds.
                            const auto last = state.lastFollowed.find(mapped.entity.GetId());
                            if (last != state.lastFollowed.end())
                            {
                                rigid.linearVelocity = (pose.pos - last->second.pos) / dt;
                                const Quat delta = Math::Normalized(pose.rot * Math::Conjugate(last->second.rot));
                                const float angle = 2.0f * std::acos(std::clamp(std::abs(delta.w), 0.0f, 1.0f));
                                const Vec3 axis(delta.x, delta.y, delta.z);
                                const float length = Math::Magnitude(axis);
                                rigid.angularVelocity = length > 1e-6f
                                                            ? axis * ((delta.w < 0.0f ? -angle : angle) / length / dt)
                                                            : Vec3(0.0f);
                            }
                            state.lastFollowed[mapped.entity.GetId()] = pose;
                        }
                    }
                    else
                    {
                        state.lastFollowed.erase(mapped.entity.GetId());
                    }
                    if (world != nullptr && !world->IsSleeping(mapped.entity))
                    {
                        ragdoll.state.anyAwake = true;
                    }
                    const auto joint = jointOf.find(mapped.entity.GetId());
                    if (joint != jointOf.end())
                    {
                        DriveJoint(scene, skeleton, ragdoll, joint->second.GetComponent<Joint>(), mapped);
                    }
                }
            }
        }

        /** @brief Yaw about world +Y of `rotation`: where it turns +Z to, flattened. */
        Quat YawOf(const Quat& rotation)
        {
            const Vec3 forward = Math::Rotate(rotation, Vec3(0.0f, 0.0f, 1.0f));
            const float yaw = std::atan2(forward.x, forward.z);
            return Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
        }

        void Update(Detail::RagdollsState& state, App& app)
        {
            Scene& scene = app.GetActiveScene();
            EntityList& entities = scene.GetEntityList();
            const std::vector<Entity> ragdolls = With(entities, ComponentType::Native<Ragdoll>());
            if (ragdolls.empty())
            {
                return;
            }
            const std::vector<Entity> candidates = With(entities, ComponentType::Native<RagdollBody>());
            for (const Entity& skeleton : ragdolls)
            {
                Ragdoll& ragdoll = skeleton.GetComponent<Ragdoll>();
                if (!Active(state, scene, skeleton))
                {
                    continue;
                }
                const std::vector<Mapped> bodies = BodiesOf(state, scene, skeleton, ragdoll, candidates);
                const bool anySimulating = std::ranges::any_of(
                    bodies, [](const Mapped& mapped) { return mapped.mode != RagdollMode::Following; });

                // The skeleton entity follows the root body: its hips bone's base pose lands on the body, yaw only.
                if (anySimulating && ragdoll.followRoot && ragdoll.rootBody.IsValid() &&
                    ragdoll.rootBody.HasComponent<RagdollBody>())
                {
                    const RagdollBody& root = ragdoll.rootBody.GetComponent<RagdollBody>();
                    const std::optional<Math::Mat4> hips =
                        root.state.bone == NoBone
                            ? std::nullopt
                            : Skeletons::GetBaseModel(scene, skeleton, root.state.bone, PoseKind::Simulation);
                    if (hips && root.state.mode != RagdollMode::Following)
                    {
                        const Transform bodyWorld = GetWorldTransform(ragdoll.rootBody);
                        const Detail::RestPose boneWorld =
                            Detail::Uncompose(Detail::RestPose{bodyWorld.pos, bodyWorld.rot}, root.offset);
                        const Detail::RestPose hipsModel = Detail::PoseOf(*hips);
                        Transform entity = GetWorldTransform(skeleton);
                        entity.rot = YawOf(Math::Normalized(boneWorld.rot * Math::Conjugate(hipsModel.rot)));
                        // Horizontally only: with the full base-pose height a body lying on the floor would put the
                        // entity (and a get-up animation's feet) under the floor. SPIKE lesson for the spec.
                        const Vec3 under = boneWorld.pos - Math::Rotate(entity.rot, hipsModel.pos * entity.scale);
                        entity.pos.x = under.x;
                        entity.pos.z = under.z;
                        SetWorldTransform(skeleton, entity);
                    }
                }

                for (const Mapped& mapped : bodies)
                {
                    const RagdollBody& body = mapped.entity.GetComponent<RagdollBody>();
                    if (mapped.mode == RagdollMode::Following)
                    {
                        // Entries hold the last simulated pose while gameplay ramps the weight down; at 0 they go.
                        if (Skeletons::GetOverrideWeight(scene, skeleton, mapped.bone) <= 0.0f)
                        {
                            Skeletons::ClearOverride(scene, skeleton, mapped.bone);
                        }
                        continue;
                    }
                    const Transform bodyWorld = GetWorldTransform(mapped.entity);
                    const Detail::RestPose boneWorld =
                        Detail::Uncompose(Detail::RestPose{bodyWorld.pos, bodyWorld.rot}, body.offset);
                    Target target;
                    target.space = PoseSpace::World;
                    target.channels = Channels::Translation | Channels::Rotation;
                    target.trs.pos = boneWorld.pos;
                    target.trs.rot = boneWorld.rot;
                    target.weight = 1.0f;
                    Skeletons::SetOverride(scene, skeleton, mapped.bone, target);
                }
            }
        }
    } // namespace

    Ragdolls Ragdolls::Install(App& app)
    {
        auto state = std::make_shared<Detail::RagdollsState>(app);
        app.AddSequentialSystem(SchedulePriority::Tick,
                                [installed = std::weak_ptr<Detail::RagdollsState>(state)](App& running) {
                                    if (const auto current = installed.lock())
                                    {
                                        Tick(*current, running);
                                    }
                                });
        app.AddSequentialSystem(SchedulePriority::Update,
                                [installed = std::weak_ptr<Detail::RagdollsState>(state)](App& running) {
                                    if (const auto current = installed.lock())
                                    {
                                        Update(*current, running);
                                    }
                                });
        return Ragdolls(std::move(state));
    }

    Ragdolls::Ragdolls(std::shared_ptr<Detail::RagdollsState> state) : m_state(std::move(state))
    {
    }

    Ragdolls::Ragdolls(Ragdolls&& other) noexcept = default;
    Ragdolls& Ragdolls::operator=(Ragdolls&& other) noexcept = default;
    Ragdolls::~Ragdolls() = default;
} // namespace Fenrir
