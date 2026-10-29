// SpawnModel (ADR-0023, ADR-0031): a model as drawn entities, SpawnNodes then a MeshRenderer per mesh.
#include "FenrirRenderer/SpawnModel.hpp"

#include <cstddef>
#include <format>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
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
         * @brief SpawnModel's second step: a MeshRenderer per rigid mesh of each spawned node, the first on the node's
         * entity and each further one on a child entity of it. Returns how many skinned meshes it skipped.
         */
        std::size_t AddMeshRenderers(Scene& scene, const Model& model, const SpawnedNodes& spawned)
        {
            EntityList& entities = scene.GetEntityList();
            std::size_t skipped = 0;
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
                    // Its vertices are in its skeleton's bind space: drawn rigidly they would look plausible and be
                    // wrong (ADR-0035), so skinned meshes wait for skinned drawing.
                    if (mesh.skeleton >= 0)
                    {
                        ++skipped;
                        continue;
                    }
                    const MeshRenderer renderer{mesh.mesh, mesh.material, true};
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
            return skipped;
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
        if (const std::size_t skipped = AddMeshRenderers(scene, *data, spawned); skipped > 0)
        {
            store.Logger().Warn("SpawnModel skipped {} skinned mesh(es) of model '{}': skinned meshes are not drawn "
                                "yet, and are never drawn unskinned",
                                skipped, spawned.root.GetComponent<Name>().Get());
        }
        return spawned.root;
    }
} // namespace Fenrir
