#pragma once

#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Value.hpp"

#include <string>
#include <string_view>

namespace Fenrir
{
    class ILogger;
}

namespace Fenrir::Serialisation
{
    /**
     * @brief Reads JSON5 text, in the full grammar, into a value tree (ADR-0077 §1), so strict JSON reads too.
     *
     * The grammar: single- and double-quoted strings with every escape (`\xHH`, `\uHHHH` and surrogate pairs, `\0`,
     * `\v`, a character escaping itself) and line continuations; hexadecimal numbers, a leading or trailing decimal
     * point, `+` and `-` signs, `Infinity` and `NaN`; keys that are identifiers, `\u` escapes in them included; `//`
     * line comments and slash-star block comments; JSON5's whitespace (the Unicode `Zs` spaces, U+FEFF and the line
     * terminators U+2028 and U+2029 among it), and a byte order mark and CRLF line endings, which read as if absent.
     * Any code point above ASCII that is not whitespace may be part of an identifier key, so the reader carries no
     * Unicode tables.
     *
     * Every node has the `Location` its text began at. An integer that fits `std::int64_t` is `Signed`, a larger one
     * `Unsigned`, and one wider still, or a number with a point or an exponent, `Double`.
     *
     * Comments are kept verbatim as trivia where `WriteText` puts them back: one on the line after a member or element
     * is its trailing comment (two there are joined by a space); others are leading comments of the member or element
     * that follows, or closing comments of their container when none follows. A comment with no such place is moved to
     * the nearest one: inside a member before its value to the member's leading comments, after the root to the root
     * container's closing comments, or to the root's leading comments when the root is not a container. A block
     * comment's line breaks are kept as LF.
     *
     * A syntax error ends the read: no tree and one error diagnostic at the character that broke the grammar, saying
     * what was expected and what was found. Nesting deeper than 256 objects and arrays is a syntax error. A duplicate
     * key is a warning at the later key, whose value is dropped so the first stands. Each diagnostic is logged once as
     * `path:line:column: message`, errors through `Error` and warnings through `Warn`, and returned in the result.
     *
     * @param text The UTF-8 text.
     * @param path The name the diagnostics give the text, as the caller wants it shown.
     * @param logger Where the diagnostics are logged.
     */
    [[nodiscard]] ReadTreeResult ReadText(std::string_view text, std::string_view path, ILogger& logger);

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
