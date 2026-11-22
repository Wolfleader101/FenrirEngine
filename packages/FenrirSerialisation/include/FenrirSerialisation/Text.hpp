#pragma once

#include "FenrirSerialisation/Value.hpp"

#include <string>

namespace Fenrir::Serialisation
{
    /**
     * @brief The JSON5 text of a value tree, in the fixed subset the engine writes (ADR-0077 §1).
     *
     * Keys are bare when they are an ECMAScript `IdentifierName`, else double-quoted. Strings are double-quoted,
     * escaping `"`, `\` and the controls (`\n`, `\t`, `\r`, `\b`, `\f`, else `\u00XX`), with other UTF-8 raw. Layout
     * is a two-space indent, LF line endings and one trailing newline, with no BOM. Block style writes one member or
     * element per line with a trailing comma; flow style (`{a: 1, b: 2}`, `[1, 2]`) is used for an array whose every
     * element is a scalar, and for an object whose every value is a scalar when it is an array element or has one
     * member. Numbers are written in their own width through `std::to_chars` (`6`, `12.5`, `0.7071068`), with
     * `Infinity`, `-Infinity` and `NaN` as literals. Comments are written where the tree holds them: leading comments
     * on their own lines at the node's indent, a trailing comment after the comma, closing comments before the
     * closing bracket. A container with a comment on one of its members or elements, or before its closing bracket,
     * is written in block style, so no comment is lost.
     *
     * The output is a function of the tree alone, so the same tree writes the same bytes on every platform.
     */
    [[nodiscard]] std::string WriteText(const Value& root);
} // namespace Fenrir::Serialisation
