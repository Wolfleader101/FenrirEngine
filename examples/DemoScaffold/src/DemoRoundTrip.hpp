#pragma once

// A smoke run's round trip of a demo scene (the scenes spec's "Demos"): what its two saves counted, and the error a
// mismatch is. The scaffold's own, kept out of DemoScaffold.hpp; its tests reach it through the scaffold's sources.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "FenrirAssets/AssetStore.hpp"

namespace FenrirDemo::Detail
{
    /**
     * @brief What one save of a demo scene wrote, which a smoke run's round trip compares: the save of the scene as it
     * ran against the save of the scene loaded back from that file, so both count what a file holds alike (an instance
     * as one block, a component a load kept opaque as the entry it writes back).
     */
    struct SceneRoundTripCounts
    {
        /** @brief The entities written, a block apiece. */
        std::size_t entities = 0;
        /** @brief The components written, an entry apiece. */
        std::size_t components = 0;

        /** @brief The counts `save` wrote. */
        [[nodiscard]] static SceneRoundTripCounts Of(const Fenrir::SceneSaveResult& save);
    };

    /**
     * @brief The error a smoke run logs when the demo scene `scene` did not load back whole: `saved` counts the save of
     * the scene as it ran, and `reloaded` the save of the scene loaded back from that file. Empty when they match.
     */
    [[nodiscard]] std::optional<std::string> RoundTripMismatch(std::string_view scene,
                                                               const SceneRoundTripCounts& saved,
                                                               const SceneRoundTripCounts& reloaded);
} // namespace FenrirDemo::Detail
