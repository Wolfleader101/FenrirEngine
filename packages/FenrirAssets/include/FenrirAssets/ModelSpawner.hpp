#pragma once

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"

namespace Fenrir
{
    class Scene;

    /**
     * @brief How AssetStore::Instantiate expands a model source (ADR-0080 §3): an interface holding no state, which a
     * package registers on the app's store with AssetStore::RegisterModelSpawner. FenrirRenderer registers its
     * SpawnModel at install, so an instanced model draws; with none registered the store spawns through its own
     * SpawnNodes, giving the node tree alone. ADR-0041's SkinningSource is the precedent.
     *
     * One spawner per store: a second registration is logged and ignored. Main thread only.
     */
    class ModelSpawner
    {
      public:
        virtual ~ModelSpawner() = default;

        /**
         * @brief Spawns `model` into `scene` under `parent` (at the top when null), as SpawnNodes does and whatever
         * else the spawner adds. Its root must be SpawnNodes' root, which SpawnNodes marks with `Instance`; an entity
         * it adds beyond SpawnNodes' gets ModelEntityId of the next number after the model's nodes, in spawn order, so
         * every spawn of a model gives the same ids. Empty, spawning nothing, when it cannot spawn.
         */
        virtual SpawnedNodes Spawn(Scene& scene, AssetHandle<Model> model, Entity parent) = 0;

      protected:
        ModelSpawner() = default;
        ModelSpawner(const ModelSpawner&) = default;
        ModelSpawner& operator=(const ModelSpawner&) = default;
    };

    /**
     * @brief The `EntityId` a model instance gives its entity numbered `number`, from 1 (ADR-0080): the number spelled
     * in lower-case Crockford base32, so 1 is `1` and 32 is `10`. A node's entity is numbered its node index plus one,
     * and an entity a spawner adds is numbered after every node (`Model::nodes.size()` plus one, then on), in spawn
     * order. The ids repeat in every instance of the model, scoped by its root, and never collide with a minted id,
     * which has five characters: a model would need over a million nodes to reach five.
     */
    [[nodiscard]] EntityId ModelEntityId(std::uint64_t number);
} // namespace Fenrir
