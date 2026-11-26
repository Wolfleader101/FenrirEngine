#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"

namespace Fenrir
{
    class ILogger;
    class TypeRegistry;
    struct TypeDescription;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /** @brief One asset an envelope holds: its top level, or one of the sub-assets in its `sub`. */
    struct EnvelopeEntry
    {
        /** @brief The sub-asset's name, its label in an asset path; empty for the file's top level. */
        std::string label;
        /** @brief The instance, of `type`. */
        AssetBox value{nullptr, nullptr};
        /** @brief The entry's type: the type expected, or one extending it, for the entry asked for; any data asset
         * type for the others. */
        const TypeDescription* type = nullptr;
    };

    /**
     * @brief What reading a data asset's envelope gave: each asset in it that read, or why the one asked for did not,
     * and the diagnostics of the whole file.
     */
    struct EnvelopeRead
    {
        /**
         * @brief Every entry that read, the top level first and then `sub`'s in the file's order; empty when the read
         * failed. An entry that did not read is left out, with its diagnostic.
         */
        std::vector<EnvelopeEntry> entries;
        /** @brief Which of `entries` is the one asked for, when the read did not fail. */
        std::size_t requested = 0;
        /** @brief Why the read failed; empty when it gave the entry asked for. */
        std::optional<LoadFailure> failure;
        /** @brief What failed it, for the store's own error: the located diagnostic, when one did. */
        std::string failureMessage;
        /**
         * @brief Every located diagnostic of the file, in the order found: ReadTree's, Upgrade's and FromTree's, each
         * already logged, and the reader's own. Of those, each is logged here but the one that failed the read: the
         * store logs that with the failure.
         */
        std::vector<Serialisation::Diagnostic> diagnostics;
        /** @brief Whether FromTree read any entry's fields in part, as a reference with no resolver makes it with no
         * diagnostic. */
        bool incomplete = false;
    };

    /**
     * @brief Why `path`, an asset path written in a file, is refused, or none when it is not (ADR-0076 §1, ADR-0085
     * §1): it is absolute, or it climbs through `..`. Only the part before its last `#`, its label, is checked, as Load
     * splits it; a `builtin:` name and a bare `#label` pass.
     */
    [[nodiscard]] std::optional<std::string> FilePathProblem(std::string_view path);

    /**
     * @brief Reads the `.asset.json5` envelope in `bytes` (ADR-0077), in these steps: ReadTree the bytes; check
     * `format` is 1; find the top level's `type` in `registry`, by its canonical or a former name, and check its type
     * (below); Upgrade `data` from `version`; FromTree it into an instance the
     * type's description constructs, through `resolvers`. Then each entry of `sub` the same way, from its own `type`,
     * `version` and `data` (ADR-0076, ADR-0079): a sub-asset holding a `sub` of its own is refused.
     *
     * The entry asked for, the top level or the sub-asset `label`, must be of `expected`; the others may be of any data
     * asset type. When it does not read, or `sub` has no `label`, the read fails. Any other entry that does not read is
     * left out with a located error, and the rest still read. Private to FenrirAssets: the store's door to it is Load.
     *
     * @param path The path its diagnostics name the file by.
     * @param label The sub-asset asked for, by its name in `sub`; none for the file's top level.
     */
    [[nodiscard]] EnvelopeRead ReadDataEnvelope(std::span<const std::byte> bytes, std::string_view path,
                                                std::optional<std::string_view> label, const TypeDescription& expected,
                                                const TypeRegistry& registry, const Serialisation::Resolvers& resolvers,
                                                ILogger& logger);
} // namespace Fenrir::Detail
