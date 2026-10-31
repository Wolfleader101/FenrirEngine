// SpawnModel (ADR-0023, ADR-0031, ADR-0041): a model as drawn entities, SpawnNodes, then SpawnSkeletons through the
// skinning source, then a MeshRenderer per mesh and a PointLight or SpotLight per light (ADR-0048).
#include "FenrirRenderer/SpawnModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    namespace
    {
        /**
         * @brief SpawnModel's last step: a MeshRenderer per mesh of each spawned node. A rigid mesh's first goes on the
         * node's entity and each further one on a child entity of it; a skinned mesh's on an entity of its own under
         * its skeleton's owner, with a SkinnedMesh naming the owner (ADR-0035).
         */
        void AddMeshRenderers(Scene& scene, const Model& model, const SpawnedNodes& spawned)
        {
            EntityList& entities = scene.GetEntityList();
            for (std::size_t i = 0; i < model.nodes.size() && i < spawned.nodes.size(); ++i)
            {
                const ModelNode& node = model.nodes[i];
                Entity entity = spawned.nodes[i];
                if (!entity.IsValid())
                {
                    continue;
                }
                std::size_t placed = 0;
                for (const ModelMesh& mesh : node.meshes)
                {
                    const MeshRenderer renderer{mesh.mesh, mesh.material, true};
                    if (mesh.skeleton >= 0)
                    {
                        // Its vertices are in its skeleton's bind space, drawn with the owner's world matrix: the
                        // entity sits under the owner at the identity, so the hierarchy reads as it draws.
                        const auto skeleton = static_cast<std::size_t>(mesh.skeleton);
                        const std::int32_t ownerNode =
                            skeleton < model.skeletonOwners.size() ? model.skeletonOwners[skeleton] : -1;
                        Entity owner = ownerNode >= 0 && static_cast<std::size_t>(ownerNode) < spawned.nodes.size()
                                           ? spawned.nodes[static_cast<std::size_t>(ownerNode)]
                                           : Entity{};
                        if (!owner.IsValid())
                        {
                            continue;
                        }
                        Entity skinned = entities.CreateEntity();
                        skinned.GetComponent<Name>().Set(node.name.c_str());
                        owner.AddChild(skinned);
                        skinned.AddComponent<MeshRenderer>(renderer);
                        skinned.AddComponent<SkinnedMesh>(SkinnedMesh{owner});
                        continue;
                    }
                    if (placed++ == 0)
                    {
                        entity.AddComponent<MeshRenderer>(renderer);
                        continue;
                    }
                    Entity extra = entities.CreateEntity();
                    extra.GetComponent<Name>().Set(std::format("{} mesh {}", node.name, placed).c_str());
                    entity.AddChild(extra);
                    extra.AddComponent<MeshRenderer>(renderer);
                }
            }
        }

        /**
         * @brief The range a light the file gives none to is given: where a light of its intensity falls to 1/256 of
         * what it gives one unit away, so a brighter light reaches further. Never under one unit.
         */
        float DefaultRange(float intensity)
        {
            return std::max(1.0f, std::sqrt(std::max(intensity, 0.0f) * 256.0f));
        }

        /** @brief A PointLight or SpotLight on the entity of each spawned node that carries a light. */
        void AddLights(const Model& model, const SpawnedNodes& spawned)
        {
            for (std::size_t i = 0; i < model.nodes.size() && i < spawned.nodes.size(); ++i)
            {
                const std::optional<ModelLight>& light = model.nodes[i].light;
                Entity entity = spawned.nodes[i];
                if (!light || !entity.IsValid())
                {
                    continue;
                }
                const Colour colour{light->colour.r, light->colour.g, light->colour.b, 1.0f};
                const float range = light->range > 0.0f ? light->range : DefaultRange(light->intensity);
                if (light->kind == ModelLightKind::Spot)
                {
                    entity.AddComponent<SpotLight>(
                        SpotLight{colour, light->intensity, range, light->innerAngle, light->outerAngle, false});
                }
                else
                {
                    entity.AddComponent<PointLight>(PointLight{colour, light->intensity, range, false});
                }
            }
        }
    } // namespace

    Entity SpawnModel(App& app, Scene& scene, AssetHandle<Model> model, Entity parent)
    {
        return SpawnModelNodes(app, scene, model, parent).root;
    }

    SpawnedNodes SpawnModelNodes(App& app, Scene& scene, AssetHandle<Model> model, Entity parent)
    {
        AssetStore* store = app.TryGetResource<AssetStore>();
        if (store == nullptr)
        {
            app.Logger()->Error("SpawnModel: the app has no AssetStore; install FenrirAssets first");
            return {};
        }
        const SpawnedNodes spawned = store->SpawnNodes(scene, model, parent);
        const Model* data = store->TryGet(model);
        if (data == nullptr || !spawned.root.IsValid())
        {
            return {};
        }
        if (!data->skeletons.empty())
        {
            if (const SkinningSourceRegistration* skinning = app.TryGetResource<SkinningSourceRegistration>())
            {
                skinning->source->SpawnSkeletons(scene, model, spawned);
            }
            else
            {
                store->Logger().Warn("SpawnModel: no skinning source is registered (install FenrirSkeleton), so the "
                                     "{} skeleton(s) of model '{}' get no SkeletonPose and its skinned meshes are not "
                                     "drawn until they do",
                                     data->skeletons.size(), spawned.root.GetComponent<Name>().Get());
            }
        }
        AddMeshRenderers(scene, *data, spawned);
        AddLights(*data, spawned);
        return spawned;
    }
} // namespace Fenrir
