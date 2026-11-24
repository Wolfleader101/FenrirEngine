#pragma once

#include "FenrirSerialisation/Diagnostic.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace Fenrir
{
    class ILogger;
}

namespace Fenrir::Serialisation
{
    /**
     * @brief Reads a file's bytes into a value tree, whether they are JSON5 text or the tree encoding: the one door
     * loaders call, so they never know which they got (ADR-0085 §2).
     *
     * A first byte of zero, which no JSON5 text starts with, is the tree encoding, read as `DecodeTree` reads it.
     * Anything else is UTF-8 text, read as `ReadText` reads it, so a byte order mark is skipped and takes no column.
     * Either way the result and its diagnostics are that reader's. An empty span is neither: no tree, and one error
     * diagnostic at line 1, column 1, logged once through `Error` as `path:line:column: message`.
     *
     * @param bytes The file's bytes, as stored.
     * @param path The name the diagnostics give the file, as the caller wants it shown.
     * @param logger Where the diagnostics are logged.
     */
    [[nodiscard]] ReadTreeResult ReadTree(std::span<const std::byte> bytes, std::string_view path, ILogger& logger);
} // namespace Fenrir::Serialisation
