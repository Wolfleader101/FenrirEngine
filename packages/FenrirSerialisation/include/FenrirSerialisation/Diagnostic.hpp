#pragma once

#include "FenrirSerialisation/Value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Fenrir::Serialisation
{
    /** @brief How bad a diagnostic is: an error is logged through `ILogger::Error`, a warning through `Warn`. */
    enum class DiagnosticSeverity
    {
        Warning,
        Error,
    };

    /**
     * @brief One located report about a file's text or contents: the file's path, the 1-based line and column it is
     * about, and a message saying what was expected and what was found.
     */
    struct Diagnostic
    {
        /** @brief Whether it is an error or a warning. */
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        /** @brief The path the caller named the text by, as given. */
        std::string path;
        /** @brief The line, counted from 1. */
        std::uint32_t line = 1;
        /**
         * @brief The column on that line, counted from 1 in code points; for the tree encoding, which has no lines, the
         * byte offset plus one on line 1.
         */
        std::uint32_t column = 1;
        /** @brief What was expected and what was found, without the location. */
        std::string message;

        friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
    };

    /** @brief The diagnostic as it reaches the logger: `path:line:column: message`. */
    [[nodiscard]] std::string FormatDiagnostic(const Diagnostic& diagnostic);

    /**
     * @brief What reading a value tree gives: the tree when one was read, and the diagnostics either way. A syntax
     * error, bytes that are not one whole tree encoding, or an empty span to `ReadTree` leaves no tree and one error
     * diagnostic; warnings come with a tree.
     */
    struct ReadTreeResult
    {
        /** @brief The tree read, or empty when the text or bytes could not be read. */
        std::optional<Value> tree;
        /** @brief Every diagnostic the read produced, in the order found, each already logged once. */
        std::vector<Diagnostic> diagnostics;
    };
} // namespace Fenrir::Serialisation
