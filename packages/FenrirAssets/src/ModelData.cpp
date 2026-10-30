#include <cstddef>
#include <format>
#include <string>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"

namespace Fenrir
{
    namespace Detail
    {
        // A model built in code is checked for what spawning relies on: parents first, bones naming a skeleton of the
        // model, and one non-bone owner per skeleton. Its sub-asset handles are not resolved, as an imported model's
        // are not once their assets may be freed apart.
        std::string PrepareAsset(Model& model, AssetWarnings&)
        {
            const auto nodeCount = static_cast<std::int32_t>(model.nodes.size());
            const auto skeletonCount = static_cast<std::int32_t>(model.skeletons.size());
            for (std::int32_t i = 0; i < nodeCount; ++i)
            {
                const ModelNode& node = model.nodes[static_cast<std::size_t>(i)];
                if (node.parent < -1 || node.parent >= i)
                {
                    return std::format("node {} ('{}') has parent {}, which is not a node before it", i, node.name,
                                       node.parent);
                }
                if (node.kind == ModelNodeKind::Bone && (node.skeleton < 0 || node.skeleton >= skeletonCount))
                {
                    return std::format("bone node {} ('{}') names skeleton {}, and the model has {}", i, node.name,
                                       node.skeleton, skeletonCount);
                }
                if (node.kind == ModelNodeKind::Bone && node.bone < 0)
                {
                    return std::format("bone node {} ('{}') has no bone index", i, node.name);
                }
            }
            if (model.skeletonOwners.size() != model.skeletons.size())
            {
                return std::format("the model has {} skeletons and {} skeleton owners", model.skeletons.size(),
                                   model.skeletonOwners.size());
            }
            for (std::size_t k = 0; k < model.skeletonOwners.size(); ++k)
            {
                const std::int32_t owner = model.skeletonOwners[k];
                if (owner < 0 || owner >= nodeCount)
                {
                    return std::format("skeleton {}'s owner is node {}, which the model does not have", k, owner);
                }
                if (model.nodes[static_cast<std::size_t>(owner)].kind == ModelNodeKind::Bone)
                {
                    return std::format("skeleton {}'s owner, node {}, is a bone", k, owner);
                }
            }
            return {};
        }
    } // namespace Detail
} // namespace Fenrir
