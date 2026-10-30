#pragma once

#include <span>
#include <string>
#include <vector>

#include "FenrirAssets/MeshData.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief Checks a mesh's skin section against its `positions` (one joint quad and one weight quad per vertex, every
     * weighted vertex summing to exactly `BoneWeightOne`, every bone index below the bone count) and computes what the
     * store derives: the bone count when it was left 0, the per-bone boxes and the box of unweighted vertices. Empty
     * when usable, else why not; vertices with no weight are a warning.
     */
    [[nodiscard]] std::string PrepareSkin(MeshSkin& skin, std::span<const Math::Vec3> positions,
                                          std::vector<std::string>& warnings);
} // namespace Fenrir::Detail
