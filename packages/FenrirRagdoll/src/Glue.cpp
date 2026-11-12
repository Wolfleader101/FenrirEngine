// The ragdoll glue (ADR-0054, amended by the spike): each tick, after FenrirSkeleton's tick pass and before physics
// steps, every active ragdoll's following bodies are moved onto their bones in the simulation pose, with the velocity
// of the move, and the ragdoll's and bodies' states are written.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
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
                    if (mapping.state.mode != RagdollMode::Following)
                    {
                        continue;
                    }

                    // 2. Modes, and 3. Follow: physics's PostTick moves the kinematic body onto the pose over the step.
                    RigidBody& rigid = body.GetComponent<RigidBody>();
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
        return Ragdolls(std::move(state));
    }

    Ragdolls::Ragdolls(std::shared_ptr<Detail::RagdollsState> state) noexcept : m_state(std::move(state))
    {
    }

    Ragdolls::Ragdolls(Ragdolls&& other) noexcept = default;
    Ragdolls& Ragdolls::operator=(Ragdolls&& other) noexcept = default;
    Ragdolls::~Ragdolls() = default;
} // namespace Fenrir
