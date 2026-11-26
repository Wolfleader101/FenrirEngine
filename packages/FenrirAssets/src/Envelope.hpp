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
    /** @brief What reading a data asset's envelope gave: the instance, or why there is none, and the diagnostics. */
    struct EnvelopeRead
    {
        /** @brief The instance, of the file's type; empty when the read failed. */
        AssetBox value{nullptr, nullptr};
        /** @brief The file's type, which is the type expected or one extending it; null when the read failed. */
        const TypeDescription* type = nullptr;
        /** @brief Why the read failed; empty when it gave an instance. */
        std::optional<LoadFailure> failure;
        /** @brief What failed it, for the store's own error: the located diagnostic, when one did. */
        std::string failureMessage;
        /**
         * @brief Every located diagnostic, in the order found: ReadTree's, Upgrade's and FromTree's, each already
         * logged, and the reader's own. Of those, a warning is logged here, and the one that failed the read is not:
         * the store logs it with the failure.
         */
        std::vector<Serialisation::Diagnostic> diagnostics;
        /** @brief Whether FromTree read the fields in part, as a reference with no resolver makes it with no
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
     * @brief Reads the `.asset.json5` envelope in `bytes` (ADR-0077) as a value of `expected` or a type extending it,
     * in these steps: ReadTree the bytes; check `format` is 1; find `type` in `registry`, by its canonical or a former
     * name, and check it is `expected` or extends it; Upgrade `data` from `version`; FromTree it into an instance the
     * type's description constructs, through `resolvers`. Its `sub` is not read yet, with a warning. Private to
     * FenrirAssets: the store's door to it is Load.
     *
     * @param path The path its diagnostics name the file by.
     */
    [[nodiscard]] EnvelopeRead ReadDataEnvelope(std::span<const std::byte> bytes, std::string_view path,
                                                const TypeDescription& expected, const TypeRegistry& registry,
                                                const Serialisation::Resolvers& resolvers, ILogger& logger);
} // namespace Fenrir::Detail
