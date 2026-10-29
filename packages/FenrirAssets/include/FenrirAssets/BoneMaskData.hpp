#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Fenrir
{
    /** @brief Whether a bone mask entry covers one bone or the bone and everything below it. */
    enum class MaskScope : std::uint8_t
    {
        Bone,
        Subtree,
    };

    /** @brief One entry of a bone mask: a bone by engine name, the weight it gets, and how far down it reaches. */
    struct BoneMaskEntry
    {
        std::string boneName;
        float weight = 1.0f;
        MaskScope scope = MaskScope::Bone;
    };

    /**
     * @brief A bone mask as plain data (ADR-0036): ordered entries naming bones by engine name. Resolved per skeleton,
     * later entries override earlier ones for the bones they cover, and bones no entry covers get 0. An animation layer
     * with no mask covers every bone at 1.
     *
     * Make one with `AssetStore::CreateBoneMask` and edit it with `IncludeInBoneMask` and `ClearBoneMask`, each edit
     * bumping its revision; or fill the entries and `Add<BoneMask>` it. Weights must be finite.
     */
    struct BoneMaskData
    {
        std::vector<BoneMaskEntry> entries;
    };
} // namespace Fenrir
