#pragma once

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    class AssetStore;
    class Scene;

    /**
     * @brief Spawns `model` into `scene` as drawn entities (ADR-0023, ADR-0031) and returns its root, in two steps:
     * the store's SpawnNodes (one entity per node with its Name, Transform and parent, under a root named after the
     * file, itself under `parent` if that is alive), then a MeshRenderer per mesh of each node, with the mesh's
     * material. A node's first mesh goes on the node's own entity; each further one on a child entity of it, named
     * after the node and the mesh's place (`Body mesh 2`), at the identity transform. Every model mesh holds one
     * submesh (the importer makes one mesh per material), so this is one MeshRenderer per mesh and submesh. The
     * animation spec adds its skeletons between the two steps.
     *
     * It needs no Renderer installed: the MeshRenderers draw once one is. Returns a null Entity, spawning nothing, for
     * a stale or empty model handle (logged by SpawnNodes).
     */
    Entity SpawnModel(AssetStore& store, Scene& scene, AssetHandle<Model> model, Entity parent = {});
} // namespace Fenrir
