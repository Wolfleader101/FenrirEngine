#pragma once

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    class App;
    class Scene;

    /**
     * @brief Spawns `model` into `scene` as drawn entities (ADR-0023, ADR-0031, ADR-0041) and returns its root, in
     * three steps:
     * 1. the app's asset store's SpawnNodes: one entity per ordinary node with its Name, Transform and parent, under a
     *    root named after the file, itself under `parent` if that is alive;
     * 2. SpawnSkeletons, through the app's skinning source (FenrirSkeleton's): a `SkeletonPose` on each skeleton's
     *    owner whose skeleton loaded, and bone attachments for the ordinary nodes below bones;
     * 3. a MeshRenderer per mesh of each node, with the mesh's material. A rigid mesh's first MeshRenderer goes on the
     *    node's own entity, and each further one on a child entity of it, named after the node and the mesh's place
     *    (`Body mesh 2`), at the identity transform. Each skinned mesh (`ModelMesh::skeleton` set) gets an entity of
     *    its own named after its node, a child of its skeleton's owner at the identity, with a MeshRenderer and a
     *    SkinnedMesh naming that owner: the node's own transform is never applied to it.
     *
     * Every model mesh holds one submesh (the importer makes one mesh per material), so this is one MeshRenderer per
     * mesh and submesh. With no skinning source registered (FenrirSkeleton not installed) it skips step 2, logged once
     * per call: the skinned meshes get their entities and SkinnedMeshes, and game code may add `SkeletonPose`s later.
     *
     * It needs no Renderer installed: the MeshRenderers draw once one is. Returns a null Entity, spawning nothing, for
     * a stale or empty model handle (logged by SpawnNodes) or an app with no AssetStore (logged).
     */
    Entity SpawnModel(App& app, Scene& scene, AssetHandle<Model> model, Entity parent = {});
} // namespace Fenrir
