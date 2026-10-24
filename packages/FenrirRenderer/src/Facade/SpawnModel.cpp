// SpawnModel (ADR-0023, ADR-0031): a model as drawn entities, SpawnNodes then a MeshRenderer per mesh.
#include "FenrirRenderer/SpawnModel.hpp"

#include <cstddef>
#include <format>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    namespace
    {
        /**
         * @brief SpawnModel's second step: a MeshRenderer per mesh of each spawned node, the first on the node's entity
         * and each further one on a child entity of it.
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
                for (std::size_t m = 0; m < node.meshes.size(); ++m)
                {
                    const MeshRenderer renderer{node.meshes[m].mesh, node.meshes[m].material, true};
                    if (m == 0)
                    {
                        entity.AddComponent<MeshRenderer>(renderer);
                        continue;
                    }
                    Entity extra = entities.CreateEntity();
                    extra.GetComponent<Name>().Set(std::format("{} mesh {}", node.name, m + 1).c_str());
                    entity.AddChild(extra);
                    extra.AddComponent<MeshRenderer>(renderer);
                }
            }
        }
    } // namespace

    Entity SpawnModel(AssetStore& store, Scene& scene, AssetHandle<Model> model, Entity parent)
    {
        const SpawnedNodes spawned = store.SpawnNodes(scene, model, parent);
        const Model* data = store.TryGet(model);
        if (data == nullptr || !spawned.root.IsValid())
        {
            return {};
        }
        // The animation spec's SpawnSkeletons and skin references go here, between the nodes and their MeshRenderers
        // (ADR-0031, ADR-0041).
        AddMeshRenderers(scene, *data, spawned);
        return spawned.root;
    }
} // namespace Fenrir
