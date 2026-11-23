#pragma once

#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Value.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Fenrir
{
    class ILogger;
}

namespace Fenrir::Serialisation
{
    /**
     * @brief The tree encoding of a value tree (glossary "Tree encoding", ADR-0083 §2, ADR-0085 §2): its compact
     * binary form, for handing a data asset to C# and for a cooked file.
     *
     * The bytes are a four-byte tag, `00 46 56 54` (a zero, which no JSON5 text starts with, then `FVT`), a version
     * byte, `01`, and then the root node. A node is one tag byte and what that tag says follows:
     *
     * - `01` null; `02` false; `03` true;
     * - `04` a signed and `05` an unsigned 64-bit integer, eight bytes; `06` a single float, four bytes; `07` a double
     *   float, eight bytes; each little-endian, the float bits as they are, so `Infinity` and every `NaN` survive;
     * - `08` a string: its length in bytes, then its UTF-8;
     * - `09` an object: its member count, then each member in order as its key (a length, then UTF-8) and its value
     *   node;
     * - `0A` an array: its element count, then each element's node in order;
     * - `0B` trivia: the comments at one node, in front of that node (after the key, for a member's value). It holds a
     *   count and that many leading comments, a count of zero or one and that many trailing comments, then a count and
     *   that many closing comments, each comment a length and UTF-8. The leading and trailing comments are those of
     *   the member or element holding the node, or the leading comments of the root; the closing comments are the
     *   node's own, which only an object or an array has.
     *
     * Every length and count is unsigned LEB128. There is no string table: each key and string is written where it is.
     *
     * @param root The tree to encode.
     * @param trivia `Trivia::Keep` writes a trivia node wherever the tree holds a comment; `Trivia::Drop` writes none,
     * which is what the C# pull and a cooker ask for.
     */
    [[nodiscard]] std::vector<std::byte> EncodeTree(const Value& root, Trivia trivia);

    /**
     * @brief Reads the tree encoding back into a value tree, so `DecodeTree(EncodeTree(t, Trivia::Keep))` equals `t`,
     * comments included. The decoded nodes have no `Location`, since bytes have no lines.
     *
     * A buffer that is not one whole encoding ends the read: no tree, and one error diagnostic saying what was
     * expected and what was found, its message naming the byte offset it is about, counted from 0. The diagnostic's
     * line is 1 and its column that offset plus one, as if the buffer were one line of bytes. A buffer is refused when
     * its tag or version is not the one above; when it ends inside a node, or inside a length, number, string or count
     * (the offset is where that piece began); when a node's tag is none of those above; when a LEB128 value overflows
     * 64 bits; when a trivia node follows another, holds two trailing comments, holds closing comments in front of a
     * scalar or a trailing comment in front of the root, or is followed by no node; when nesting is deeper than 256
     * objects and arrays, as in text; and when bytes follow the root node. The diagnostic is logged once through
     * `Error` as `path:line:column: message` and returned in the result.
     *
     * @param bytes The encoding, from its tag byte.
     * @param path The name the diagnostic gives the buffer, as the caller wants it shown.
     * @param logger Where the diagnostic is logged.
     */
    [[nodiscard]] ReadTreeResult DecodeTree(std::span<const std::byte> bytes, std::string_view path, ILogger& logger);
} // namespace Fenrir::Serialisation
