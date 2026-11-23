# 08: `EncodeTree` and `DecodeTree`, with and without trivia

**What to build:** the compact binary form of a value tree (ADR-0083 §2, ADR-0085 §2, glossary "Tree encoding"). C# will pull a data asset through it, and a cooked file will be written in it. It encodes the tree itself, so the same bridge reads it.

- **`EncodeTree(const Value&, Trivia) -> std::vector<std::byte>`** writes, in order (the spec, "The tree encoding and `ReadTree`"):
  - a four-byte tag whose first byte is zero;
  - a version byte;
  - one node per tag byte. Lengths and counts are LEB128, numbers are little-endian in their own width, and strings and keys are length-prefixed UTF-8. Members stay in order, and trivia is its own node kind.

  `Trivia::Keep` keeps comments and `Trivia::Drop` leaves them out. There is no string table.
- **`DecodeTree(std::span<const std::byte>, path, ILogger&) -> ReadTreeResult`** reverses it. A truncated or malformed buffer is one diagnostic naming the byte offset, and gives no tree.
- **Its own files:** the encoding lives in its own `Encoding` header and source, so this ticket shares only one line of `Tests/CMakeLists.txt` with the bridge chain.

**Blocked by:** 02.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/Encoding.hpp`, `src/Encoding.cpp`, `Tests/Encoding.cpp`, one line of `Tests/CMakeLists.txt`)

**Status:** resolved

- [x] Each golden, read and then encoded and decoded, equals its tree. With `Trivia::Keep` the trivia is included in the comparison, and with `Trivia::Drop` the tree has none.
- [x] Every number width, `Infinity` and `NaN` survives the round trip bit for bit.
- [x] Each golden's encoding is smaller than its text.
- [x] A truncated buffer and an unknown node tag each give one diagnostic with the offset, and no tree.
