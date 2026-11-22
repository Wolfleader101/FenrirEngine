# 01: The package, `Value` and `WriteText`: hand-built sword and scene trees write the golden bytes

**What to build:** a new package, `FenrirSerialisation`, holding the value tree and the JSON5 writer, as ADR-0077 and ADR-0079 §1 decide. A tree built in code writes the exact bytes of the research's two golden files, so the writer's output is pinned before anything reads or bridges. Nothing reads text yet (ticket 02) and nothing walks a described value (ticket 03).

- **The package:** `packages/FenrirSerialisation/`, a `STATIC` library linking `FenrirTypes` and `FenrirLogger` `PUBLIC` and nothing else: no `libs/` directory, no submodule, no file system (ADR-0079 §1, ADR-0010). Namespace `Fenrir::Serialisation`. The root `CMakeLists.txt` adds it with no `FENRIR_ENABLE_*` switch, and `README.md`'s package list names it.
- **`Value`** (the spec, "The value tree"): one of `Object`, `Array`, `String`, `Number`, `Bool` and `Null`, with `Kind()` and checked accessors. An `Object` holds members in insertion order, with a linear `Find(key)`. A `Number` is a signed integer, an unsigned integer, a single float or a double float. Each node has an optional `Location` (1-based `line` and `column`), which code-built nodes leave empty. Trivia: `leadingComments` and an optional `trailingComment` on each member and element, `closingComments` on each container, and leading comments on the root, each kept verbatim with its delimiters. Equality ignores locations; `Equals(a, b, Trivia::Compare)` compares trivia too.
- **`WriteText(const Value&) -> std::string`** (the spec, "The JSON5 writer"):
  - Keys are bare when they are an ECMAScript `IdentifierName`, else double-quoted.
  - Strings are double-quoted with the listed escapes, and other UTF-8 is written raw.
  - Layout is a two-space indent, LF line endings and one trailing newline.
  - Block style has a trailing comma. Flow style is used for an array of scalars, and for an all-scalar object that is an array element or has one member.
  - Numbers are written through `std::to_chars` in their own width, with `Infinity`, `-Infinity` and `NaN` as literals.
  - Trivia is re-emitted where it was.
  - The output is a function of the tree alone.
- **The goldens:** `sword.asset.json5` and `level1.scene.json5`, the JSON5 samples in `.scratch/fenrir-serialisation/research/format-fit.md` on branch `research/format-fit` (a78ddc75). They are 688 and 1,451 bytes. Commit them as fixtures under `packages/FenrirSerialisation/Tests/fixtures/`, named by a `FENRIR_SERIALISATION_FIXTURES` definition as `FENRIR_ASSETS_FIXTURES` is, and never regenerate them. If a sample's bytes disagree with the spec's rules (its flow rule, say), stop and ask rather than editing the golden.

**Blocked by:** none.

**Touches:** `packages/FenrirSerialisation/` (new: `CMakeLists.txt`, `include/FenrirSerialisation/`, `src/`, `Tests/`), the root `CMakeLists.txt`, `README.md`

**Status:** ready-for-agent

- [ ] `FenrirSerialisation` builds on msvc-x64-debug, links only `FenrirTypes` and `FenrirLogger`, and its `PublicHeaders` test compiles each header alone.
- [ ] A sword tree and a scene tree built in code, comments included, write the two goldens byte for byte.
- [ ] One test per emit rule: a bare key and a quoted one, each escape, the four flow-rule branches, each number width, `Infinity`, `-Infinity` and `NaN`.
- [ ] Writing one tree twice gives the same bytes, and equality ignores locations but `Trivia::Compare` does not ignore comments.
