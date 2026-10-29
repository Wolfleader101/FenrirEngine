#include "Rigs.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        /** @brief Collects the skeleton entity and the skinned mesh entities at and below `entity`. */
        void Collect(const Fenrir::Entity& entity, Rig& rig)
        {
            if (entity.HasComponent<Fenrir::SkeletonPose>() && !rig.skeleton.IsValid())
            {
                rig.skeleton = entity;
            }
            if (entity.HasComponent<Fenrir::SkinnedMesh>())
            {
                rig.meshes.push_back(entity);
            }
            entity.ForEachChild([&](const Fenrir::Entity& child) { Collect(child, rig); });
        }
    } // namespace

    Fenrir::AssetHandle<Fenrir::Clip> Rig::Clip(Fenrir::App& app, std::size_t index) const
    {
        const Fenrir::Model* data = app.GetResource<Fenrir::AssetStore>().TryGet(model);
        return data != nullptr && index < data->clips.size() ? data->clips[index] : Fenrir::AssetHandle<Fenrir::Clip>{};
    }

    Rig SpawnRig(Fenrir::App& app, Fenrir::Scene& scene, std::string_view file, Vec3 position, float yaw, float scale)
    {
        Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        const auto model = store.Load<Fenrir::Model>(file, scene);
        if (!model.Ok())
        {
            app.Logger()->Error("AnimationDemo: {} did not load from the asset root", file);
            return {};
        }
        Rig rig;
        rig.model = model.handle;
        rig.root = Fenrir::SpawnModel(app, scene, model.handle);
        if (!rig.root.IsValid())
        {
            app.Logger()->Error("AnimationDemo: {} did not spawn", file);
            return {};
        }
        auto& transform = rig.root.GetComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.rot = Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
        transform.scale = Vec3(scale);
        Collect(rig.root, rig);
        if (!rig.skeleton.IsValid())
        {
            app.Logger()->Error("AnimationDemo: {} spawned no skeleton entity", file);
            return {};
        }
        return rig;
    }

    Fenrir::AssetHandle<Fenrir::Clip> LoadClip(Fenrir::App& app, Fenrir::Scene& scene, std::string_view file)
    {
        Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        const auto model = store.Load<Fenrir::Model>(file, scene);
        const Fenrir::Model* data = model.Ok() ? store.TryGet(model.handle) : nullptr;
        if (data == nullptr || data->clips.empty())
        {
            app.Logger()->Error("AnimationDemo: {} has no clip to load", file);
            return {};
        }
        return data->clips.front();
    }

    void Play(Fenrir::Entity skeleton, Fenrir::AssetHandle<Fenrir::Clip> clip, Fenrir::ClipLoop loop, float speed)
    {
        Fenrir::Animator animator;
        animator.clip = clip;
        animator.loop = loop;
        animator.speed = speed;
        if (skeleton.HasComponent<Fenrir::Animator>())
        {
            skeleton.GetComponent<Fenrir::Animator>() = animator;
        }
        else
        {
            skeleton.AddComponent<Fenrir::Animator>(animator);
        }
    }

    void DrawBones(Fenrir::App& app, Fenrir::Scene& scene, Fenrir::DebugDraw& draw, Fenrir::Entity skeleton, bool bones,
                   bool names)
    {
        if (!bones && !names)
        {
            return;
        }
        const Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        const Fenrir::SkeletonData* data = store.TryGet(skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton);
        if (data == nullptr)
        {
            return;
        }
        const Fenrir::ShapeOptions onTop{.thickness = 2.0f, .depth = Fenrir::DepthMode::OnTop};
        for (std::size_t i = 0; i < data->BoneCount(); ++i)
        {
            const auto bone = static_cast<std::int32_t>(i);
            const std::optional<Fenrir::Math::Mat4> world =
                Fenrir::Skeletons::GetBoneWorldMatrix(scene, skeleton, bone);
            if (!world)
            {
                return;
            }
            const Vec3 joint((*world)[3]);
            if (bones)
            {
                const std::int32_t parent = data->bones[i].parent;
                if (parent != Fenrir::NoBone)
                {
                    if (const auto up = Fenrir::Skeletons::GetBoneWorldMatrix(scene, skeleton, parent))
                    {
                        draw.Line(Vec3((*up)[3]), joint, Fenrir::Colour::FromSrgb(0xFFD040FF), onTop);
                    }
                }
                draw.Axes(*world, 0.08f, onTop);
            }
            if (names)
            {
                draw.WorldText(joint + Vec3(0.03f, 0.0f, 0.0f), data->EngineName(bone), Fenrir::Colour::White,
                               {.size = 12.0f});
            }
        }
    }

    void AddStage(Fenrir::App& app, Fenrir::Scene& scene, float width, float depth)
    {
        Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
        const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene);
        if (!cube.Ok() || !lit.Ok())
        {
            app.Logger()->Error("AnimationDemo: the floor's cube or builtin:lit did not load");
            return;
        }
        const auto floorMaterial = store.CreateMaterial(lit.handle, scene);
        store.SetColour(floorMaterial, "baseColour", Fenrir::Colour::FromSrgb(0x5A6470FF));
        Fenrir::EntityList& entities = scene.GetEntityList();
        Fenrir::Entity floor = entities.CreateEntity();
        floor.GetComponent<Fenrir::Name>().Set("Floor");
        auto& transform = floor.GetComponent<Fenrir::Transform>();
        transform.pos = {0.0f, -0.05f, 0.0f};
        transform.scale = {width, 0.1f, depth};
        floor.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube.handle, floorMaterial, true});

        Fenrir::Entity light = entities.CreateEntity();
        light.GetComponent<Fenrir::Name>().Set("Sun");
        light.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(-0.9f, Vec3(1.0f, 0.0f, 0.0f)) *
                                                      Fenrir::Math::AngleAxis(0.5f, Vec3(0.0f, 1.0f, 0.0f));
        light.AddComponent<Fenrir::DirectionalLight>();
        scene.AddResource<Fenrir::AmbientLight>(Fenrir::AmbientLight{Fenrir::Colour{0.35f, 0.38f, 0.45f, 1.0f}, 1.0f});
    }
} // namespace AnimationDemo
