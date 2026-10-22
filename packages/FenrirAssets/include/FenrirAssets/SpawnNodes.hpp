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
         * @brief One entity per model node, indexed as Model::nodes is. In the foundation every node is an entity; the
         * animation spec leaves bones out, and their entries null (ADR-0031).
         */
        std::vector<Entity> nodes;
    };
} // namespace Fenrir
