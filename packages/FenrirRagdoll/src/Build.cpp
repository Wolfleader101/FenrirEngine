// BuildRagdoll and DestroyRagdoll (ticket 53): a description spawned as body and joint entities, root first.

#include <cstddef>
#include <string>
#include <unordered_map>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirRagdoll/Ragdoll.hpp"
#include "FenrirRagdoll/RagdollDescription.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Frames.hpp"

namespace Fenrir
{
    SpawnedRagdoll BuildRagdoll(App& app, Scene& scene, Entity skeleton, const RagdollDescription& description)
    {
        SpawnedRagdoll spawned;
        if (!description.error.empty())
        {
            spawned.error = "BuildRagdoll: the description was refused: " + description.error;
            return spawned;
        }
        if (!skeleton.IsValid() || !skeleton.HasComponent<SkeletonPose>())
        {
            spawned.error = "BuildRagdoll: the entity is not a skeleton entity (no SkeletonPose)";
            return spawned;
        }
        const AssetStore& store = app.GetResource<AssetStore>();
        const SkeletonData* data = store.TryGet(skeleton.GetComponent<SkeletonPose>().skeleton);
        if (data == nullptr)
        {
            spawned.error = "BuildRagdoll: the skeleton asset does not resolve";
            return spawned;
        }
        const std::vector<Detail::RestPose> rest = Detail::RestPoses(*data);
        const Transform world = GetWorldTransform(skeleton);
        const std::uint32_t group = description.group != 0 ? description.group : skeleton.GetId();

        EntityList& entities = scene.GetEntityList();
        spawned.parent = entities.CreateEntity();
        spawned.parent.GetComponent<Name>().Set(
            ("Ragdoll of " + std::string(skeleton.GetComponent<Name>().Get())).c_str());

        std::unordered_map<std::string, Entity> bodyOf;
        for (const RagdollPart& part : description.parts)
        {
            const std::string boneName(part.bone.Get().value_or(""));
            const std::int32_t bone = data->FindBone(boneName);
            if (bone == NoBone)
            {
                spawned.error = "BuildRagdoll: no bone is named '" + boneName + "'";
                DestroyRagdoll(scene, Entity(), spawned);
                return {.error = spawned.error};
            }
            // The body at the rest pose, in world space: skeleton × bone × offset.
            const Detail::RestPose boneWorld{world.pos +
                                                 Math::Rotate(world.rot, rest[static_cast<std::size_t>(bone)].pos),
                                             Math::Normalized(world.rot * rest[static_cast<std::size_t>(bone)].rot)};
            const Detail::RestPose bodyWorld = Detail::Compose(boneWorld, part.offset);

            Entity body = entities.CreateEntity();
            body.GetComponent<Name>().Set((boneName + " body").c_str());
            body.GetComponent<Transform>() = Transform(bodyWorld.pos, bodyWorld.rot, Math::Vec3(1.0f));
            body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Kinematic, .mass = part.mass});
            body.AddComponent<Collider>(Collider{part.shape});
            body.AddComponent<CollisionGroup>(CollisionGroup{group});
            RagdollBody mapping;
            mapping.ragdoll = skeleton;
            mapping.bone = part.bone;
            mapping.offset = part.offset;
            mapping.strengthScale = part.strengthScale;
            body.AddComponent<RagdollBody>(mapping);
            spawned.parent.AddChild(body);
            spawned.bodies.push_back(body);
            bodyOf[boneName] = body;

            const std::string parentName(part.parent.Get().value_or(""));
            if (parentName.empty())
            {
                continue;
            }
            const auto parentBody = bodyOf.find(parentName);
            if (parentBody == bodyOf.end())
            {
                spawned.error = "BuildRagdoll: part '" + boneName + "' names parent '" + parentName +
                                "', which is not an earlier part";
                DestroyRagdoll(scene, Entity(), spawned);
                return {.error = spawned.error};
            }
            Entity joint = entities.CreateEntity();
            joint.GetComponent<Name>().Set((boneName + " joint").c_str());
            const JointFrame anchor{world.pos + Math::Rotate(world.rot, part.jointAnchor),
                                    Math::Normalized(world.rot * part.jointFrame)};
            const JointFrames frames = FrameFromWorld(parentBody->second, body, anchor);
            joint.AddComponent<Joint>(Joint{.bodyA = parentBody->second,
                                            .bodyB = body,
                                            .frameA = frames.frameA,
                                            .frameB = frames.frameB,
                                            .kind = part.joint});
            spawned.parent.AddChild(joint);
            spawned.joints.push_back(joint);
        }

        Ragdoll ragdoll;
        ragdoll.mode = RagdollMode::Following;
        ragdoll.strength = description.strength;
        ragdoll.group = group;
        ragdoll.rootBody = spawned.bodies.empty() ? Entity() : spawned.bodies.front();
        skeleton.AddComponent<Ragdoll>(ragdoll);
        return spawned;
    }

    void DestroyRagdoll(Scene& scene, Entity skeleton, const SpawnedRagdoll& spawned)
    {
        EntityList& entities = scene.GetEntityList();
        for (const Entity& joint : spawned.joints)
        {
            if (joint.IsValid())
            {
                entities.DestroyEntity(joint);
            }
        }
        for (const Entity& body : spawned.bodies)
        {
            if (body.IsValid())
            {
                entities.DestroyEntity(body);
            }
        }
        if (spawned.parent.IsValid())
        {
            entities.DestroyEntity(spawned.parent);
        }
        if (skeleton.IsValid())
        {
            if (skeleton.HasComponent<Ragdoll>())
            {
                skeleton.RemoveComponent<Ragdoll>();
            }
            Skeletons::ClearOverrides(scene, skeleton);
        }
    }
} // namespace Fenrir
