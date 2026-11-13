// The ragdoll glue (ADR-0054, amended by the spike): each tick, after FenrirSkeleton's tick pass and before physics
// steps, every active ragdoll's bodies get their motion types and its following bodies are moved onto their bones in
// the simulation pose, with the velocity of the move; each joint between two of its bodies gets its motor mode, and a
// powered child's joint its target from the base pose and its torque and spring from the strength; and the ragdoll's
// and bodies' states are written. Each frame,
// after physics interpolates and before the pose pipeline, the skeleton entity follows a simulating root body and each
// simulating body's bone is written as a world pose override, cleared once every body follows and every weight is 0.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Frames.hpp"
#include "Overrides.hpp"

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
            /** @brief Each following body's pose last tick, by entity id: the next tick's velocity comes from it. */
            std::unordered_map<std::uint32_t, Pose> lastFollowed;
            /** @brief Skeleton entities warned about as inactive, until they are active again. */
            std::unordered_set<std::uint32_t> inactiveWarned;
            /** @brief Body entities warned about as skipped, with why, until they resolve (or are skipped for another
             * reason). */
            std::unordered_map<std::uint32_t, std::string> skippedWarned;
        };
    } // namespace Detail

    namespace
    {
        using Math::Quat;
        using Math::Vec3;

        std::vector<Entity> With(EntityList& entities, ComponentType type)
        {
            std::vector<Entity> found;
            const ComponentType types[] = {type};
            entities.ForEachWith(types, [&](std::uint32_t id) { found.push_back(entities.GetEntity(id)); });
            return found;
        }

        /** @brief How a log line names an entity: its id and name. */
        std::string LogName(const Entity& entity)
        {
            return "entity " + std::to_string(entity.GetId()) + " ('" + entity.GetComponent<Name>().Get() + "')";
        }

        /** @brief Whether `skeleton` is a live entity of `entities` with a `Ragdoll`. */
        bool IsLiveRagdoll(EntityList& entities, const Entity& skeleton)
        {
            return skeleton.IsValid() && entities.HasEntity(skeleton.GetId()) &&
                   entities.GetEntity(skeleton.GetId()) == skeleton && skeleton.HasComponent<Ragdoll>();
        }

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

        /** @brief Why the ragdoll on `skeleton` cannot run now, or empty when it can. */
        std::string Inactivity(Scene& scene, const Entity& skeleton)
        {
            if (!skeleton.HasComponent<SkeletonPose>() || Skeletons::GetPoseState(scene, skeleton) != PoseState::Ready)
            {
                return "it has no Ready SkeletonPose";
            }
            if (skeleton.GetComponent<SkeletonPose>().timing != PoseTiming::Tick)
            {
                return "its skeleton is frame-posed: a ragdoll follows the simulation pose, so set "
                       "SkeletonPose::timing to PoseTiming::Tick";
            }
            return {};
        }

        /** @brief Warns once that `body` is skipped for `why`, again only when the reason changes. */
        void WarnSkipped(Detail::RagdollsState& state, const Entity& body, const std::string& why)
        {
            auto [entry, added] = state.skippedWarned.try_emplace(body.GetId(), why);
            if (added || entry->second != why)
            {
                entry->second = why;
                state.app->Logger()->Warn("The RagdollBody on " + LogName(body) + " is skipped: " + why);
            }
        }

        /** @brief The rotation `delta` as a rotation vector over `dt`: radians per second about its axis. */
        Vec3 AngularVelocity(const Quat& delta, float dt)
        {
            const Quat shortest = delta.w < 0.0f ? -delta : delta;
            const Vec3 axis(shortest.x, shortest.y, shortest.z);
            const float sine = Math::Magnitude(axis);
            if (sine < 1e-7f)
            {
                return Vec3(0.0f);
            }
            const float angle = 2.0f * std::atan2(sine, shortest.w);
            return axis * (angle / sine / dt);
        }

        /** @brief Turns a ragdoll joint's motor off, every other field left: hinges and swing-twist joints, the two
         * kinds a ragdoll's joints are. */
        void MotorOff(JointKind& kind)
        {
            if (kind.Is<HingeJoint>())
            {
                kind.As<HingeJoint>().motor.mode = JointMotorMode::Off;
            }
            else if (kind.Is<SwingTwistJoint>())
            {
                kind.As<SwingTwistJoint>().motor.mode = JointMotorMode::Off;
            }
        }

        /**
         * @brief frameB's rotation relative to frameA with both bodies where the base pose puts them: the child bone's
         * base-pose rotation relative to its parent bone (its base local when the parent part's bone is its parent
         * bone, else from both bones' base model matrices, across the unmapped bones between), through both offsets
         * and the joint's frames. `nullopt` when the pose cannot be read.
         */
        std::optional<Quat> BaseTarget(Scene& scene, const Entity& skeleton, const SkeletonData* data,
                                       const RagdollBody& parent, const RagdollBody& child, const Joint& joint)
        {
            Math::Mat4 parentModel(1.0f);
            Math::Mat4 childModel(1.0f);
            const auto bone = static_cast<std::size_t>(child.state.bone);
            if (data != nullptr && bone < data->BoneCount() && data->bones[bone].parent == parent.state.bone)
            {
                const std::optional<Transform> local =
                    Skeletons::GetBaseLocal(scene, skeleton, child.state.bone, PoseKind::Simulation);
                if (!local)
                {
                    return std::nullopt;
                }
                childModel = TransformToMat4(*local);
            }
            else
            {
                const std::optional<Math::Mat4> parentBase =
                    Skeletons::GetBaseModel(scene, skeleton, parent.state.bone, PoseKind::Simulation);
                const std::optional<Math::Mat4> childBase =
                    Skeletons::GetBaseModel(scene, skeleton, child.state.bone, PoseKind::Simulation);
                if (!parentBase || !childBase)
                {
                    return std::nullopt;
                }
                parentModel = *parentBase;
                childModel = *childBase;
            }
            const Quat parentBody = Detail::PoseOf(parentModel * TransformToMat4(parent.offset)).rot;
            const Quat childBody = Detail::PoseOf(childModel * TransformToMat4(child.offset)).rot;
            return Math::Normalized(Math::Conjugate(joint.frameA.rotation) * Math::Conjugate(parentBody) * childBody *
                                    joint.frameB.rotation);
        }

        /** @brief The turn of `rotation` about its X axis, in radians within a half turn either way: a hinge's angle.
         */
        float AngleAboutX(Quat rotation)
        {
            rotation = rotation.w < 0.0f ? -rotation : rotation;
            return 2.0f * std::atan2(rotation.x, rotation.w);
        }

        /** @brief Full strength's maximum torque per kilogram of the child body (the spec's strength decision). */
        constexpr float TorquePerKilogram = 30.0f;

        /** @brief Where a ragdoll's base pose is read: its skeleton entity, and its skeleton asset when the store has
         * it (to tell a bone's skeleton parent). */
        struct PoseSource
        {
            Scene& scene;
            const Entity& skeleton;
            const SkeletonData* data;
        };

        /**
         * @brief Drives `joint`'s motor towards the base pose (`Tick` step 4): Position mode, the target from
         * `BaseTarget`, no target velocity, and the torque and spring of `strength` (taken within [0, 1]) and the
         * child's strength scale. A pose that cannot be read keeps the last target.
         */
        void Drive(const PoseSource& pose, float strength, const Entity& parentBody, const Entity& childBody,
                   Joint& joint)
        {
            const RagdollBody& child = childBody.GetComponent<RagdollBody>();
            strength = std::clamp(strength, 0.0f, 1.0f);
            const float torque = std::max(0.0f, strength * child.strengthScale * TorquePerKilogram *
                                                    childBody.GetComponent<RigidBody>().mass);
            const JointSpring spring{.frequency = Math::Lerp(1.0f, 10.0f, strength), .damping = 1.0f};
            const std::optional<Quat> target =
                BaseTarget(pose.scene, pose.skeleton, pose.data, parentBody.GetComponent<RagdollBody>(), child, joint);
            if (joint.kind.Is<HingeJoint>())
            {
                JointMotor& motor = joint.kind.As<HingeJoint>().motor;
                motor.mode = JointMotorMode::Position;
                motor.targetVelocity = 0.0f;
                if (target)
                {
                    motor.targetPosition = AngleAboutX(*target);
                }
                motor.maxForce = torque;
                motor.spring = spring;
            }
            else if (joint.kind.Is<SwingTwistJoint>())
            {
                SwingTwistMotor& motor = joint.kind.As<SwingTwistJoint>().motor;
                motor.mode = JointMotorMode::Position;
                motor.targetAngularVelocity = Vec3(0.0f);
                if (target)
                {
                    motor.targetRotation = *target;
                }
                motor.maxTorque = torque;
                motor.spring = spring;
            }
        }

        /**
         * @brief `Tick` steps 2 and 4 for the joints. A ragdoll's joints are those between two of its bodies (bodyA
         * the parent, bodyB the child, as the builder joins them): each is driven while both bodies resolved this tick
         * and its child is powered, and turned off otherwise (a powered body skipped since included). Joints game code
         * made to anything else, the root body's included, are left alone: the root gets no drive. Each joint is
         * written on its own, so the order joints are visited in does not matter.
         */
        void ApplyJointMotors(EntityList& entities, const PoseSource& pose, const Ragdoll& ragdoll,
                              const std::unordered_map<std::uint32_t, Entity>& resolved,
                              const std::vector<Entity>& joints)
        {
            const auto ofThisRagdoll = [&entities, &pose](const Entity& body) {
                return body.IsValid() && entities.HasEntity(body.GetId()) && entities.GetEntity(body.GetId()) == body &&
                       body.HasComponent<RagdollBody>() && body.GetComponent<RagdollBody>().ragdoll == pose.skeleton;
            };
            const auto resolvedThisTick = [&resolved](const Entity& body) {
                const auto found = resolved.find(body.GetId());
                return found != resolved.end() && found->second == body;
            };
            for (const Entity& entity : joints)
            {
                Joint& joint = entity.GetComponent<Joint>();
                if (!ofThisRagdoll(joint.bodyA) || !ofThisRagdoll(joint.bodyB))
                {
                    continue;
                }
                if (resolvedThisTick(joint.bodyA) && resolvedThisTick(joint.bodyB) &&
                    joint.bodyB.GetComponent<RagdollBody>().state.mode == RagdollMode::Powered)
                {
                    Drive(pose, ragdoll.strength, joint.bodyA, joint.bodyB, joint);
                }
                else
                {
                    MotorOff(joint.kind);
                }
            }
        }

        void Tick(Detail::RagdollsState& state, App& app)
        {
            Scene& scene = app.GetActiveScene();
            EntityList& entities = scene.GetEntityList();
            const std::vector<Entity> bodies = With(entities, ComponentType::Native<RagdollBody>());
            const std::vector<Entity> ragdolls = With(entities, ComponentType::Native<Ragdoll>());
            const std::vector<Entity> joints = With(entities, ComponentType::Native<Joint>());
            const AssetStore* store = app.TryGetResource<AssetStore>();
            const PhysicsWorld* world = PhysicsWorld::Of(scene);
            const float dt = static_cast<float>(app.GetTime().tickRate);
            std::unordered_map<std::uint32_t, Detail::Pose> followed;

            // Bodies whose ragdoll is no Ragdoll entity of this scene belong to no ragdoll the loop below visits.
            for (const Entity& body : bodies)
            {
                RagdollBody& mapping = body.GetComponent<RagdollBody>();
                if (!IsLiveRagdoll(entities, mapping.ragdoll))
                {
                    mapping.state.bone = NoBone;
                    WarnSkipped(state, body, "its ragdoll is not a live entity of the scene with a Ragdoll");
                }
            }

            for (const Entity& skeleton : ragdolls)
            {
                Ragdoll& ragdoll = skeleton.GetComponent<Ragdoll>();
                const std::string inactivity = Inactivity(scene, skeleton);
                if (!inactivity.empty())
                {
                    ragdoll.state = RagdollState{.inactive = true};
                    if (state.inactiveWarned.insert(skeleton.GetId()).second)
                    {
                        app.Logger()->Warn("The Ragdoll on " + LogName(skeleton) + " is inactive: " + inactivity);
                    }
                    continue;
                }
                state.inactiveWarned.erase(skeleton.GetId());

                RagdollState next;
                std::unordered_map<std::uint32_t, Entity> resolved;
                for (const Entity& body : bodies)
                {
                    RagdollBody& mapping = body.GetComponent<RagdollBody>();
                    if (mapping.ragdoll != skeleton)
                    {
                        continue;
                    }
                    // 1. Resolve.
                    mapping.state.mode = EffectiveMode(ragdoll, mapping);
                    const std::string_view name = Detail::NameOf(mapping.bone);
                    mapping.state.bone = Skeletons::FindBone(scene, skeleton, name);
                    if (!body.HasComponent<RigidBody>())
                    {
                        mapping.state.bone = NoBone;
                        WarnSkipped(state, body, "it has no RigidBody");
                        continue;
                    }
                    if (mapping.state.bone == NoBone)
                    {
                        WarnSkipped(state, body,
                                    mapping.bone.IsTerminated()
                                        ? "no bone of its skeleton is named '" + std::string(name) + "'"
                                        : std::string("its bone name has no terminator"));
                        continue;
                    }
                    state.skippedWarned.erase(body.GetId());
                    resolved.emplace(body.GetId(), body);
                    ++next.bodies;
                    if (world != nullptr && !world->IsSleeping(body))
                    {
                        next.anyAwake = true;
                    }
                    // 2. Modes (the joints' motors after the loop). A body made dynamic is not moved and keeps the
                    // velocity its RigidBody holds: the last following move's. One made kinematic is not moved either:
                    // the follow below takes it there.
                    RigidBody& rigid = body.GetComponent<RigidBody>();
                    if (mapping.state.mode != RagdollMode::Following)
                    {
                        rigid.motionType = MotionType::Dynamic;
                        continue;
                    }

                    // 3. Follow: physics's PostTick moves the kinematic body onto the pose over the step.
                    rigid.motionType = MotionType::Kinematic;
                    const Detail::Pose pose = Detail::PoseOf(
                        *Skeletons::GetBoneWorldMatrix(scene, skeleton, mapping.state.bone, PoseKind::Simulation) *
                        TransformToMat4(mapping.offset));
                    SetWorldTransform(body, Transform(pose.pos, pose.rot, GetWorldTransform(body).scale));
                    // A body that becomes dynamic takes the velocity its RigidBody holds, so it is kept as the move's.
                    const auto last = state.lastFollowed.find(body.GetId());
                    if (last == state.lastFollowed.end())
                    {
                        rigid.linearVelocity = Vec3(0.0f);
                        rigid.angularVelocity = Vec3(0.0f);
                    }
                    else
                    {
                        rigid.linearVelocity = (pose.pos - last->second.pos) / dt;
                        rigid.angularVelocity =
                            AngularVelocity(Math::Normalized(pose.rot * Math::Conjugate(last->second.rot)), dt);
                    }
                    followed[body.GetId()] = pose;
                }
                // 2 and 4. Motors: off, or for a powered child towards the base pose.
                const PoseSource pose{scene, skeleton,
                                      store == nullptr ? nullptr
                                                       : store->TryGet(skeleton.GetComponent<SkeletonPose>().skeleton)};
                ApplyJointMotors(entities, pose, ragdoll, resolved, joints);
                // 5. State.
                ragdoll.state = next;
            }
            // A body that stopped following, was skipped or is gone starts again with no earlier pose.
            state.lastFollowed = std::move(followed);
        }

        /** @brief A body that resolved last tick, with its mapping. */
        struct Resolved
        {
            Entity entity;
            const RagdollBody* mapping;
        };

        /** @brief `skeleton`'s bodies that resolved last tick, in storage order. */
        std::vector<Resolved> ResolvedBodies(const std::vector<Entity>& bodies, const Entity& skeleton)
        {
            std::vector<Resolved> resolved;
            for (const Entity& body : bodies)
            {
                const RagdollBody& mapping = body.GetComponent<RagdollBody>();
                if (mapping.ragdoll == skeleton && mapping.state.bone != NoBone && body.HasComponent<RigidBody>())
                {
                    resolved.push_back(Resolved{body, &mapping});
                }
            }
            return resolved;
        }

        /**
         * @brief Where `body` puts its bone, in world space: its world pose × offset⁻¹. The body's pose has no scale,
         * so the bone's simulation-pose world scale is put back first, which makes the inverse of the follow (bone ×
         * offset, scale dropped) exact for a uniformly scaled bone.
         */
        std::optional<Math::Mat4> BoneWorld(Scene& scene, const Entity& skeleton, const Entity& body,
                                            const RagdollBody& mapping)
        {
            const std::optional<Math::Mat4> bone =
                Skeletons::GetBoneWorldMatrix(scene, skeleton, mapping.state.bone, PoseKind::Simulation);
            if (!bone)
            {
                return std::nullopt;
            }
            Vec3 scale;
            Vec3 unusedPos;
            Quat unusedRot;
            Math::Decompose(*bone, unusedPos, unusedRot, scale);
            const Transform world = GetWorldTransform(body);
            return TransformToMat4(Transform(world.pos, world.rot, scale)) *
                   Math::Inverse(TransformToMat4(mapping.offset));
        }

        /**
         * @brief The twist of `rotation` about +Y (its swing-twist decomposition): a turn about the vertical followed
         * by a tip about any horizontal axis twists by exactly that turn, so a body that tipped over onto its front,
         * back or side still faces the way it faced standing, at any tip short of upside down. `nullopt` there, where
         * no twist is defined.
         */
        std::optional<Quat> Yaw(const Quat& rotation)
        {
            const float length = std::sqrt(rotation.w * rotation.w + rotation.y * rotation.y);
            if (length < 1e-6f)
            {
                return std::nullopt;
            }
            return Quat(rotation.w / length, 0.0f, rotation.y / length, 0.0f);
        }

        /**
         * @brief Moves the skeleton entity so the hips bone's base pose under it lands on the root body's bone
         * horizontally and in yaw: its height and scale kept, upright.
         */
        void FollowRoot(Scene& scene, const Entity& skeleton, const Entity& root, const RagdollBody& mapping)
        {
            const std::optional<Math::Mat4> hips = BoneWorld(scene, skeleton, root, mapping);
            const std::optional<Math::Mat4> base =
                Skeletons::GetBaseModel(scene, skeleton, mapping.state.bone, PoseKind::Simulation);
            if (!hips || !base)
            {
                return;
            }
            // The entity's world matrix that would put the base pose's hips exactly on the bone: only its yaw is kept.
            const Detail::Pose under = Detail::PoseOf(*hips * Math::Inverse(*base));
            Transform entity = GetWorldTransform(skeleton);
            if (const std::optional<Quat> yaw = Yaw(under.rot))
            {
                entity.rot = *yaw;
            }
            const Vec3 offset = Math::Rotate(entity.rot, Vec3((*base)[3]) * entity.scale);
            entity.pos.x = (*hips)[3].x - offset.x;
            entity.pos.z = (*hips)[3].z - offset.z;
            SetWorldTransform(skeleton, entity);
        }

        void Update(App& app)
        {
            Scene& scene = app.GetActiveScene();
            EntityList& entities = scene.GetEntityList();
            // A skeleton that lost its Ragdoll, or is gone, keeps its entries (ADR-0038) but no record of them.
            if (Detail::RagdollOverrides* written = scene.GetResource<Detail::RagdollOverrides>())
            {
                written->Prune([&entities](const Entity& skeleton) { return IsLiveRagdoll(entities, skeleton); });
            }
            const std::vector<Entity> ragdolls = With(entities, ComponentType::Native<Ragdoll>());
            if (ragdolls.empty())
            {
                return;
            }
            const std::vector<Entity> bodies = With(entities, ComponentType::Native<RagdollBody>());
            Detail::RagdollOverrides& overrides = Detail::RagdollOverrides::In(scene);

            for (const Entity& skeleton : ragdolls)
            {
                const Ragdoll& ragdoll = skeleton.GetComponent<Ragdoll>();
                if (!Inactivity(scene, skeleton).empty())
                {
                    continue; // Tick warned
                }
                const std::vector<Resolved> resolved = ResolvedBodies(bodies, skeleton);
                const bool allFollowing = std::ranges::all_of(
                    resolved, [](const Resolved& body) { return body.mapping->state.mode == RagdollMode::Following; });

                // 1. Follow root: while the root body is not following (ADR-0054's 2026-11-12 amendment): a following
                // root is already where the entity puts it, and following it would fight a character moving the entity.
                if (ragdoll.followRoot)
                {
                    const auto root = std::ranges::find_if(
                        resolved, [&ragdoll](const Resolved& body) { return body.entity == ragdoll.rootBody; });
                    if (root != resolved.end() && root->mapping->state.mode != RagdollMode::Following)
                    {
                        FollowRoot(scene, skeleton, root->entity, *root->mapping);
                    }
                }

                // 2. Override targets: each simulating or powered body's bone where the body puts it, at weight 1;
                // how much each counts is gameplay's override weight, never written here.
                if (!allFollowing)
                {
                    Detail::WrittenBones& written = overrides.Of(skeleton);
                    for (const Resolved& body : resolved)
                    {
                        if (body.mapping->state.mode == RagdollMode::Following)
                        {
                            continue;
                        }
                        const std::optional<Math::Mat4> bone = BoneWorld(scene, skeleton, body.entity, *body.mapping);
                        if (!bone)
                        {
                            continue;
                        }
                        const Detail::Pose pose = Detail::PoseOf(*bone);
                        Skeletons::SetOverride(scene, skeleton, body.mapping->state.bone,
                                               Target{.space = PoseSpace::World,
                                                      .trs = Transform(pose.pos, pose.rot, Vec3(1.0f)),
                                                      .channels = Channels::Translation | Channels::Rotation,
                                                      .weight = 1.0f});
                        written.Add(body.mapping->state.bone);
                    }
                    continue;
                }

                // 3. Clearing: with every body following, the entries hold the last simulated pose while gameplay
                // ramps the weights down, and go once every weight of the bones written reads 0.
                Detail::WrittenBones* written = overrides.Find(skeleton);
                if (written == nullptr)
                {
                    continue;
                }
                std::erase_if(written->bones, [&scene, &skeleton](std::int32_t bone) {
                    return !Skeletons::GetOverride(scene, skeleton, bone).has_value(); // cleared by someone else
                });
                const bool allZero = std::ranges::all_of(written->bones, [&scene, &skeleton](std::int32_t bone) {
                    return Skeletons::GetOverrideWeight(scene, skeleton, bone) <= 0.0f;
                });
                if (allZero)
                {
                    overrides.Clear(scene, skeleton);
                }
            }
        }
    } // namespace

    bool Ragdolls::ClearOverrides(Scene& scene, Entity skeleton)
    {
        if (!skeleton.IsValid() || !skeleton.HasComponent<Ragdoll>())
        {
            return false;
        }
        EntityList& entities = scene.GetEntityList();
        for (const Entity& body : With(entities, ComponentType::Native<RagdollBody>()))
        {
            const RagdollBody& mapping = body.GetComponent<RagdollBody>();
            if (mapping.ragdoll == skeleton)
            {
                Skeletons::ClearOverride(scene, skeleton,
                                         Skeletons::FindBone(scene, skeleton, Detail::NameOf(mapping.bone)));
            }
        }
        // And the bones of bodies destroyed since they were written.
        if (Detail::RagdollOverrides* overrides = scene.GetResource<Detail::RagdollOverrides>())
        {
            overrides->Clear(scene, skeleton);
        }
        return true;
    }

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
        // After physics's AfterTicks has interpolated the bodies, before the pose pipeline in PostUpdate.
        app.AddSequentialSystem(SchedulePriority::Update,
                                [installed = std::weak_ptr<Detail::RagdollsState>(state)](App& running) {
                                    if (installed.lock())
                                    {
                                        Update(running);
                                    }
                                });
        return Ragdolls(std::move(state));
    }

    Ragdolls::Ragdolls(std::shared_ptr<Detail::RagdollsState> state) noexcept : m_state(std::move(state))
    {
    }

    Ragdolls::Ragdolls(Ragdolls&& other) noexcept = default;
    Ragdolls& Ragdolls::operator=(Ragdolls&& other) noexcept = default;
    Ragdolls::~Ragdolls() = default;
} // namespace Fenrir
