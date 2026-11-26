#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir
{
    class ILogger;
    class TypeRegistry;
    struct FieldInfo;
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

    /** @brief What a reference in a file names: the asset, or why it names none. */
    struct ReferenceRead
    {
        /** @brief The target's slot, or the empty handle when `problem` says why there is none. */
        ReferenceValue value;
        /**
         * @brief Empty when `value` names the target; otherwise what was expected and what was found instead, such as
         * "expected `AssetsTest::Sword` or a type extending it, found `AssetsTest::WeaponData`".
         */
        std::string problem;
    };

    /** @brief How the store takes part in one envelope's read. */
    struct EnvelopeHooks
    {
        /**
         * @brief Names the target of a reference the file holds, given its path as written (already past the path
         * rules) and its field, whose `type.target` names the target's type. A problem becomes one located error at
         * the path's string. None leaves every reference empty, counted as the bridge counts a missing resolver.
         */
        std::function<ReferenceRead(std::string_view path, const FieldInfo& field)> readReference;
        /**
         * @brief Told each entry that will read, by its label (empty for the top level) and its type, once every
         * entry is checked and before any entry's fields, so before any of its references resolve. A read told of an
         * entry never fails after.
         */
        std::function<void(std::string_view label, const TypeDescription& type)> reading;
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
     * (below), its `version` and its `data`; check each entry of `sub` the same way, from its own `type`, `version` and
     * `data` (ADR-0076, ADR-0079), refusing a sub-asset holding a `sub` of its own. Then, once `hooks.reading` is told
     * of every entry that passed, each in turn: Upgrade its `data` from its `version`, and FromTree it into an
     * instance its type's description constructs, its references named through `hooks.readReference` and no entity
     * callbacks, since a data asset holds no entity.
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
                                                const TypeRegistry& registry, const EnvelopeHooks& hooks,
                                                ILogger& logger);
} // namespace Fenrir::Detail
