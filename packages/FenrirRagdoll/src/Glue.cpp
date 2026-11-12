// The ragdoll glue (ADR-0054, amended by the spike): each tick, after FenrirSkeleton's tick pass and before physics
// steps, every active ragdoll's bodies get their motion types and its following bodies are moved onto their bones in
// the simulation pose, with the velocity of the move, and the ragdoll's and bodies' states are written. Each frame,
// after physics interpolates and before the pose pipeline, the skeleton entity follows a simulating root body and each
// simulating body's bone is written as a world pose override, cleared once every body follows and every weight is 0.

#include <algorithm>
#include <cmath>
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
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Components.hpp"
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

        void Tick(Detail::RagdollsState& state, App& app)
        {
            Scene& scene = app.GetActiveScene();
            EntityList& entities = scene.GetEntityList();
            const std::vector<Entity> bodies = With(entities, ComponentType::Native<RagdollBody>());
            const std::vector<Entity> ragdolls = With(entities, ComponentType::Native<Ragdoll>());
            const PhysicsWorld* world = PhysicsWorld::Of(scene);
            const float dt = static_cast<float>(app.GetTime().tickRate);
            std::unordered_map<std::uint32_t, Detail::Pose> followed;

            // Bodies whose ragdoll is no Ragdoll entity of this scene belong to no ragdoll the loop below visits.
            for (const Entity& body : bodies)
            {
                RagdollBody& mapping = body.GetComponent<RagdollBody>();
                const Entity& ragdoll = mapping.ragdoll;
                const bool live = ragdoll.IsValid() && entities.HasEntity(ragdoll.GetId()) &&
                                  entities.GetEntity(ragdoll.GetId()) == ragdoll && ragdoll.HasComponent<Ragdoll>();
                if (!live)
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
                for (const Entity& body : bodies)
                {
                    RagdollBody& mapping = body.GetComponent<RagdollBody>();
                    if (mapping.ragdoll != skeleton)
                    {
                        continue;
                    }
                    // 1. Resolve.
                    mapping.state.mode = EffectiveMode(ragdoll, mapping);
                    const std::string_view name = mapping.bone.Get().value_or(std::string_view());
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
                    ++next.bodies;
                    if (world != nullptr && !world->IsSleeping(body))
                    {
                        next.anyAwake = true;
                    }
                    // 2. Modes. A body made dynamic is not moved and keeps the velocity its RigidBody holds: the last
                    // following move's. One made kinematic is not moved either: the follow below takes it there.
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
                // 4. State.
                ragdoll.state = next;
            }
            // A body that stopped following, was skipped or is gone starts again with no earlier pose.
            state.lastFollowed = std::move(followed);
        }

        /** @brief A body the glue drove last tick, with its mapping. */
        struct Driven
        {
            Entity entity;
            const RagdollBody* mapping;
        };

        /** @brief `skeleton`'s bodies that last tick resolved, in storage order. */
        std::vector<Driven> DrivenBodies(const std::vector<Entity>& bodies, const Entity& skeleton)
        {
            std::vector<Driven> driven;
            for (const Entity& body : bodies)
            {
                const RagdollBody& mapping = body.GetComponent<RagdollBody>();
                if (mapping.ragdoll == skeleton && mapping.state.bone != NoBone && body.HasComponent<RigidBody>())
                {
                    driven.push_back(Driven{body, &mapping});
                }
            }
            return driven;
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
         * @brief The turn about +Y that `rotation` faces: where it turns +Z, flattened, with its +Y's flattened
         * direction standing in as +Z tips towards the vertical, so a body lying on its front or back still faces the
         * way its head points. `nullopt` when neither gives a direction.
         */
        std::optional<Quat> Yaw(const Quat& rotation)
        {
            const Vec3 forward = Math::Rotate(rotation, Vec3(0.0f, 0.0f, 1.0f));
            const Vec3 up = Math::Rotate(rotation, Vec3(0.0f, 1.0f, 0.0f));
            const float x = forward.x - forward.y * up.x;
            const float z = forward.z - forward.y * up.z;
            if (x * x + z * z < 1e-10f)
            {
                return std::nullopt;
            }
            return Math::AngleAxis(std::atan2(x, z), Vec3(0.0f, 1.0f, 0.0f));
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
                const std::vector<Driven> driven = DrivenBodies(bodies, skeleton);
                const bool allFollowing = std::ranges::all_of(
                    driven, [](const Driven& body) { return body.mapping->state.mode == RagdollMode::Following; });

                // 1. Follow root: while the root body is not following (any body, ADR-0054, but a following root is
                // where the entity already puts it, and following it would fight a character moving the entity).
                if (ragdoll.followRoot)
                {
                    const auto root = std::ranges::find_if(
                        driven, [&ragdoll](const Driven& body) { return body.entity == ragdoll.rootBody; });
                    if (root != driven.end() && root->mapping->state.mode != RagdollMode::Following)
                    {
                        FollowRoot(scene, skeleton, root->entity, *root->mapping);
                    }
                }

                // 2. Override targets: each simulating or powered body's bone where the body puts it, at weight 1;
                // how much each counts is gameplay's override weight, never written here.
                if (!allFollowing)
                {
                    Detail::WrittenBones& written = overrides.Of(skeleton);
                    for (const Driven& body : driven)
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
                    for (const std::int32_t bone : written->bones)
                    {
                        Skeletons::ClearOverride(scene, skeleton, bone);
                    }
                    overrides.bySkeleton.erase(skeleton.GetId());
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
                Skeletons::ClearOverride(
                    scene, skeleton,
                    Skeletons::FindBone(scene, skeleton, mapping.bone.Get().value_or(std::string_view())));
            }
        }
        // Bones of bodies destroyed since they were written.
        if (Detail::RagdollOverrides* overrides = scene.GetResource<Detail::RagdollOverrides>())
        {
            if (const Detail::WrittenBones* written = overrides->Find(skeleton))
            {
                for (const std::int32_t bone : written->bones)
                {
                    Skeletons::ClearOverride(scene, skeleton, bone);
                }
                overrides->bySkeleton.erase(skeleton.GetId());
            }
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
