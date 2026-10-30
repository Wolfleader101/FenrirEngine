#pragma once

#include <vector>

#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /** @brief The entities AssetStore::SpawnNodes made for a model. */
    struct SpawnedNodes
    {
        /**
         * @brief The entity holding the whole model: named after its file, at the identity transform, with every root
         * node of the model as its child. Null when nothing was spawned.
         */
        Entity root;
        /**
         * @brief One entity per model node, indexed as Model::nodes is: ordinary nodes and synthetic owners. Bones get
         * no entity, so their entries are null (ADR-0031). An ordinary node below bones is a child of its nearest
         * spawned ancestor, its Transform its rest place relative to that ancestor.
         */
        std::vector<Entity> nodes;
    };
} // namespace Fenrir
