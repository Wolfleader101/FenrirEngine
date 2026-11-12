// BuildRagdoll and DestroyRagdoll: a description spawned as ordinary body and joint entities under one parent, root
// first, and removed again.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirRagdoll/Builder.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Frames.hpp"

namespace Fenrir
{
    namespace
    {
        std::string_view NameOf(const BoneName& name)
        {
            return name.Get().value_or(std::string_view());
        }

        SpawnedRagdoll Refused(std::string error)
        {
            SpawnedRagdoll refused;
            refused.error = "BuildRagdoll: " + std::move(error);
            return refused;
        }

        /** @brief The joint kind as built: friction from the description, its motor off. */
        JointKind AsSpawned(JointKind kind, float friction)
        {
            if (kind.Is<HingeJoint>())
            {
                kind.As<HingeJoint>().friction = friction;
                kind.As<HingeJoint>().motor.mode = JointMotorMode::Off;
            }
            else if (kind.Is<SwingTwistJoint>())
            {
                kind.As<SwingTwistJoint>().friction = friction;
                kind.As<SwingTwistJoint>().motor.mode = JointMotorMode::Off;
            }
            else if (kind.Is<SliderJoint>())
            {
                kind.As<SliderJoint>().friction = friction;
                kind.As<SliderJoint>().motor.mode = JointMotorMode::Off;
            }
            return kind;
        }

        /** @brief `entity` and everything under it, each child before its parent. */
        void CollectBelow(const Entity& entity, std::vector<Entity>& out)
        {
            entity.ForEachChild([&](const Entity& child) { CollectBelow(child, out); });
            out.push_back(entity);
        }
    } // namespace

    SpawnedRagdoll BuildRagdoll(App& app, Scene& scene, Entity skeleton, const RagdollDescription& description)
    {
        // Everything is checked before anything is spawned, so a refusal leaves the scene as it was.
        if (!description.error.empty())
        {
            return Refused("the description was refused: " + description.error);
        }
        if (description.parts.empty())
        {
            return Refused("the description has no parts");
        }
        if (!skeleton.IsValid() || !skeleton.HasComponent<SkeletonPose>() ||
            Skeletons::GetPoseState(scene, skeleton) != PoseState::Ready)
        {
            return Refused("the entity has no Ready SkeletonPose");
        }
        if (skeleton.HasComponent<Ragdoll>())
        {
            return Refused("the skeleton entity already has a Ragdoll");
        }
        const AssetStore* store = app.TryGetResource<AssetStore>();
        const SkeletonData* data =
            store == nullptr ? nullptr : store->TryGet(skeleton.GetComponent<SkeletonPose>().skeleton);
        if (data == nullptr)
        {
            return Refused("the skeleton asset is not in the asset store");
        }
        std::vector<std::int32_t> bones;
        bones.reserve(description.parts.size());
        std::unordered_map<std::string_view, std::size_t> partOf;
        for (std::size_t i = 0; i < description.parts.size(); ++i)
        {
            const RagdollPart& part = description.parts[i];
            const std::string_view name = NameOf(part.bone);
            bones.push_back(data->FindBone(name));
            if (bones.back() == NoBone)
            {
                return Refused("no bone is named '" + std::string(name) + "'");
            }
            const std::string_view parent = NameOf(part.parent);
            if ((i == 0) != parent.empty() || (i > 0 && !partOf.contains(parent)))
            {
                return Refused("part '" + std::string(name) + "' names parent '" + std::string(parent) +
                               "': the first part is the root, with no parent, and every other part's parent is an "
                               "earlier part's bone");
            }
            partOf.emplace(name, i);
        }

        const std::vector<Math::Mat4> rest = Detail::RestModels(*data);
        const Math::Mat4 skeletonWorld = GetWorldMatrix(skeleton);
        const Math::Quat skeletonTurn = Detail::PoseOf(skeletonWorld).rot;
        // The id 0 is no group: Entity::Null is never a live entity's id, so no other ragdoll takes it by default.
        const std::uint32_t group =
            description.group != 0 ? description.group : (skeleton.GetId() != 0 ? skeleton.GetId() : Entity::Null);

        EntityList& entities = scene.GetEntityList();
        SpawnedRagdoll spawned;
        spawned.parent = entities.CreateEntity();
        spawned.parent.GetComponent<Name>().Set(
            ("Ragdoll of " + std::string(skeleton.GetComponent<Name>().Get())).c_str());

        for (std::size_t i = 0; i < description.parts.size(); ++i)
        {
            const RagdollPart& part = description.parts[i];
            // skeleton × bone × offset, as the glue follows it, then reduced to a position and rotation.
            const Detail::Pose body =
                Detail::PoseOf(skeletonWorld * rest[static_cast<std::size_t>(bones[i])] * TransformToMat4(part.offset));

            Entity entity = entities.CreateEntity();
            entity.GetComponent<Name>().Set((std::string(NameOf(part.bone)) + " body").c_str());
            spawned.parent.AddChild(entity);
            SetWorldTransform(entity, Transform(body.pos, body.rot, Math::Vec3(1.0f)));
            entity.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Kinematic, .mass = part.mass});
            entity.AddComponent<Collider>(Collider{part.shape});
            entity.AddComponent<CollisionGroup>(CollisionGroup{group});
            RagdollBody mapping;
            mapping.ragdoll = skeleton;
            mapping.bone = part.bone;
            mapping.offset = part.offset;
            mapping.strengthScale = part.strengthScale;
            entity.AddComponent<RagdollBody>(mapping);
            spawned.bodies.push_back(entity);
        }

        // Joints after every body, root first: each joint's frames come from both bodies' rest world poses.
        for (std::size_t i = 1; i < description.parts.size(); ++i)
        {
            const RagdollPart& part = description.parts[i];
            const Entity& parentBody = spawned.bodies[partOf.at(NameOf(part.parent))];
            const JointFrame anchor{Math::Vec3(skeletonWorld * Math::Vec4(part.jointAnchor, 1.0f)),
                                    Math::Normalized(skeletonTurn * part.jointFrame.rotation)};
            const JointFrames frames = FrameFromWorld(parentBody, spawned.bodies[i], anchor);

            Entity entity = entities.CreateEntity();
            entity.GetComponent<Name>().Set((std::string(NameOf(part.bone)) + " joint").c_str());
            spawned.parent.AddChild(entity);
            entity.AddComponent<Joint>(Joint{.bodyA = parentBody,
                                             .bodyB = spawned.bodies[i],
                                             .frameA = frames.frameA,
                                             .frameB = frames.frameB,
                                             .kind = AsSpawned(part.joint, description.jointFriction),
                                             .collideConnected = false});
            spawned.joints.push_back(entity);
        }

        Ragdoll ragdoll;
        ragdoll.strength = description.strength;
        ragdoll.group = group;
        ragdoll.rootBody = spawned.bodies.front();
        skeleton.AddComponent<Ragdoll>(ragdoll);
        return spawned;
    }

    void DestroyRagdoll(Scene& scene, Entity skeleton)
    {
        if (!skeleton.IsValid() || !skeleton.HasComponent<Ragdoll>())
        {
            return;
        }
        // The glue's override entries are on its bodies' bones; game code's on other bones stay.
        Ragdolls::ClearOverrides(scene, skeleton);
        const Entity root = skeleton.GetComponent<Ragdoll>().rootBody;
        if (root.IsValid())
        {
            const Entity parent = root.GetComponent<Relationship>().parent;
            std::vector<Entity> spawned;
            CollectBelow(parent.IsValid() ? parent : root, spawned);
            EntityList& entities = scene.GetEntityList();
            for (const Entity& entity : spawned)
            {
                entities.DestroyEntity(entity);
            }
        }
        skeleton.RemoveComponent<Ragdoll>();
    }
} // namespace Fenrir
