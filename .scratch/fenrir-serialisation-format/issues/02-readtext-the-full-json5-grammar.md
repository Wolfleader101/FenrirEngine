# 02: `ReadText`: the full JSON5 grammar, located diagnostics, the `json5-tests` fixtures, and the goldens round-trip with comments

**What to build:** the engine's own JSON5 reader (ADR-0077: no library), so any JSON5 or strict JSON file reads into the value tree with its comments, and a broken one says where. Read then written, each golden gives back its own bytes.

- **`ReadText(std::string_view text, std::string_view path, ILogger&) -> ReadTreeResult`** (the spec, "The JSON5 reader") parses the full grammar:
  - both quote styles, with every escape and line continuations;
  - hex numbers, a leading or trailing dot, signs, `Infinity` and `NaN`;
  - identifier keys;
  - `//` and `/* */` comments, kept as trivia where ticket 01's writer puts them back;
  - JSON5's whitespace set, a BOM and CRLF.

  Each node gets its `Location`. A number keeps the width its text implies: an integer that fits in `i64` is signed, a larger one unsigned, and one with a point or exponent a double.
- **Errors:**
  - A syntax error ends the parse with no tree and one `Diagnostic` (`path`, `line`, `column`, what was expected and what was found).
  - A duplicate key is a located warning; the first value stands.
  - Nesting deeper than 256 is a syntax error.
  - Diagnostics are logged as `path:line:column: message`, errors through `Error` and warnings through `Warn`, and are also returned in the result.
- **`json5-tests`:** a committed copy of the `json5/json5-tests` cases (MIT, with its licence file) under `Tests/fixtures/json5-tests/`. Pin the commit you take in the folder's README.

**Blocked by:** 01.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/`, `src/`, `Tests/`, `Tests/CMakeLists.txt`, `Tests/fixtures/json5-tests/` new)

**Status:** ready-for-agent

- [ ] Every valid `json5-tests` case parses. Every invalid case fails with one diagnostic whose line and column the test asserts.
- [ ] Each golden reads then writes back its own bytes, comments included.
- [ ] A duplicate key warns at its location and keeps the first value. A file nested 257 deep fails. A BOM and CRLF each read as if absent.
- [ ] A diagnostic reaches the logger as `path:line:column: message` and is in the result.
