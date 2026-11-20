Status: ready-for-agent

# FenrirSerialisation: the value tree, the engine-owned JSON5 reader and writer, the bridge to described values, upgraders, located diagnostics and the tree encoding

Origin: the serialisation map (`.scratch/fenrir-serialisation/map.md`), its "Spec boundaries" entry's second spec, gathering tickets 03 (text formats), 12 (format and versioning), 15 (the package's place) and the tree parts of 16 (the tree encoding) and 20 (`ReadTree` sniffing), settled in grilling sessions on 2026-11-19 after the measured comparison on branch `research/format-fit`. It builds on the types spec (`.scratch/fenrir-serialisation-types/spec.md`: `FenrirTypes`, the registry, the visitor, the kinds and doc commands) and cites rather than restates it.

| ADR | Decision |
|---|---|
| 0077 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0079 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0083 | C# data assets are classes materialised from the store's value tree, and scenes reach C# through the Assets facade |
| 0085 | The development formats promise a cooked form path identity, compiled-in descriptions, the tree encoding and one asset source seam |
| 0074 | Types read from and written to files are described by the API description and registered with FenrirTypes from generated code |
| 0075 | A data asset is a game-defined plain struct in the asset store, with single inheritance and polymorphism through references |
| 0076 | An asset's identity in files is its root-relative path, and a tool rewrites references when a file moves |
| 0078 | A scene file is authoring data holding described components by entity id and described scene resources, and is read once |
| 0010 | FenrirPhysics hides its physics backend at compile time (the rule that keeps libraries private; this package has none) |

## Prerequisite

The types spec's `FenrirTypes` is merged: the type registry looked up by canonical type name, the one visitor interface over a value's fields (name, kind, metadata, a reference to the value), each field's default, the kinds `string`, `list`, `optional`, `reference`, `entity`, `fixed-string` and `path` beside the plain ones, `@version` and `@formerly` compiled into the registration, a type-erased upgrader slot per type, and a described tree shape for every bound type a file may hold (an array of N scalars for the maths types). Tickets 01 to 02 and 08 link `FenrirLogger` alone and may start off `main` before it; the integration branch `fenrir-serialisation-format` is cut from `main` once it has merged, and the bridge tickets stack on it. Nothing in `FenrirAssets`, `FenrirScene` or `FenrirECS` changes here.

## Problem Statement

Nothing is serialised today (ticket 08). The one text format the engine reads is the `.cubemap` file, six face lines parsed by hand; models, images, shader containers and C# assemblies are the files it knows. The twelve pinned submodules hold no JSON, YAML or TOML library, and ADR-0077 chose to add none: no C++ library keeps comments across a rewrite, every library path still needs an engine-owned writer for declaration order, float text and `Infinity`, and a JSON5 reader and writer measured at 1,250 to 2,000 lines against submodules of 16k to 40k.

The engine has no value tree and no diagnostic that names a line. `LoadDiagnostic` carries a `path`, a `LoadFailure` reason and an `importerMessage`; `LoadResult` is `Succeeded`, `Partial` or `Failed` with `failedUnits`. `ILogger` formats through `std::format` into `Log`, `Info`, `Warn`, `Error` and `Fatal` with no notion of a file position. The API description exists only at build time, for plain data, and refuses strings and lists; the types spec widens it and gives a run-time registry, but nothing walks a described value into a file's shape or back.

Scene files, data assets and dev-time data (ADR-0078, 0061, 0070) all need one package that owns that shape: a tree with comments as trivia, a reader of the full JSON5 grammar, a writer whose output is a function of the data alone, a bridge over the registry's visitor, upgraders applied in order, and the compact binary form C# pulls a data asset through (ADR-0083) and a cooked file is written in (ADR-0085).

## Solution

All in a new package `FenrirSerialisation` (`packages/FenrirSerialisation/`), a `STATIC` library linking `FenrirTypes` and `FenrirLogger` `PUBLIC` and nothing else: no `libs/` directory, no submodule, no file system, no asset, scene or envelope (ADR-0079 §1, ADR-0010). Everything below is in namespace `Fenrir::Serialisation`.

### The value tree

- **`Value`** is one of `Object`, `Array`, `String`, `Number`, `Bool` and `Null` (ADR-0079 §1), with `Kind()` and checked accessors. An `Object` is an ordered list of members (`key`, `Value`), kept in the order read or added, so declaration order on write and byte-identical rewrites cost nothing; `Find(key)` is linear, which a file's objects are small enough for.
- **A `Number`** is one of a signed integer, an unsigned integer, a single-precision and a double-precision float, so the writer prints each in its own shortest form and a 64-bit integer does not pass through a `double`.
- **Every node carries a `Location`** (`line`, `column`, both 1-based) when it came from text, and none when built in code.
- **Trivia:** each member and element has `leadingComments` (the comment lines above it), an optional `trailingComment` (on the same line after its value), and each container has `closingComments` (before its closing bracket); the root value has leading comments too. Comments are kept verbatim with their `//` or `/* */` delimiters. Blank lines are not trivia and are not kept.
- **Equality** compares values and ignores locations; `Equals(a, b, Trivia::Compare)` compares trivia too.

### The JSON5 reader

- **`ReadText(std::string_view text, std::string_view path, ILogger&) -> ReadTreeResult`** parses the full JSON5 grammar (both quote styles with every escape and line continuation, hex, leading and trailing dot, signs, `Infinity`, `NaN`, identifier keys, `//` and `/* */` comments, JSON5's whitespace set, a BOM, CRLF), so every strict JSON file reads too. `ReadTreeResult` holds the tree when one was read and the `Diagnostics` either way.
- **A syntax error ends the parse**: no tree, one `Diagnostic` with `path`, `line`, `column` and the expected-and-found message. The tolerance ADR-0077 §7 promises is the bridge's (below): a bad field, not bad syntax, is what loads partly.
- **A duplicate key** is a located warning; the first value stands and the later one is dropped. **Nesting deeper than 256** is a syntax error, so a hostile file cannot overflow the stack.
- **Diagnostics go through the logger** as `path:line:column: message`, errors through `Error` and warnings through `Warn`, and are returned in the result for the caller that counts them (`FenrirAssets`' results, ADR-0079 §3 and §5).

### The JSON5 writer

- **`WriteText(const Value&) -> std::string`** emits the fixed subset (ADR-0077 §1): bare identifier keys (ECMAScript `IdentifierName`, else double-quoted, so `"Fenrir::Sky"` is quoted and `cubemap` is not), double-quoted strings escaping `"`, `\` and controls (`\n`, `\t`, `\r`, `\b`, `\f`, else `\u00XX`) with other UTF-8 written raw, two-space indent, LF line endings, one trailing newline, no BOM.
- **Block and flow style.** Block style writes one member or element per line with a trailing comma. Flow style, with `, ` separators and no trailing comma, is used for an array whose every element is a scalar, and for an object whose every value is a scalar when it is an array element (a table row) or has exactly one member. Everything else is block style. This is the rule that reproduces the research's samples byte for byte.
- **Numbers:** integers without a point, single floats through `std::to_chars(float)` and doubles through `std::to_chars(double)` in their shortest round-trip form (`6`, `12.5`, `0.7071068`), `Infinity`, `-Infinity` and `NaN` as literals, never hex.
- **Trivia is re-emitted**: leading comments on their own lines at the member's indent, a trailing comment after the comma, closing comments before the bracket, so a tool that reads a tree, edits it and writes it keeps every comment.
- **The output is a function of the tree alone**: no clock, no path, no pointer value, so the same data writes the same bytes on every platform.

### The bridge

- **`ToTree(const void* value, const TypeInfo&, const TypeRegistry&, const Resolvers&, const Value* prior = nullptr) -> Value`** walks the registry's visitor (ADR-0074 §4) and writes fields in declaration order, omitting every field equal to its default (ADR-0075 §7) and every disengaged `optional`. A struct is an `Object`, a `list` an `Array`, an enum its enumerator name as a `String`, a `string` and a `fixed-string` a `String`, a bound type its registered shape (`position: [0, 1.8, 0]`), a tagged union an `Object` whose first member is `kind` (the tag's enumerator name) followed by the active kind's fields.
- **Comments survive a value rewrite** through `prior`: the tree the value was read from. Every node of the new tree whose key path exists in `prior` takes its trivia; a comment on a field no longer written is dropped. Without `prior` the tree has no trivia.
- **`FromTree(const Value&, const TypeInfo&, const TypeRegistry&, const Resolvers&, void* value, ILogger&) -> BridgeResult`** fills a default-constructed value field by field and is tolerant: a field absent from the tree keeps its default; a field of the wrong kind, an integer out of its kind's range, a float in an integer field, an unknown enumerator or `null` in a field whose kind does not admit it takes its default with one located diagnostic and is counted in `defaulted`; a key matching no field and no `@formerly` name is warned and dropped, counted in `unknownFields` (ADR-0077 §6); a key matching a field's `@formerly` name reads into it, and when both names are present the current one wins and the former is dropped with a warning; a `fixed-string` longer than N-1 bytes is cut on a UTF-8 boundary with a diagnostic (ADR-0078 §6).
- **Unknown types are the caller's to keep.** A canonical type name the registry lacks makes `FromTree` return `BridgeStatus::UnknownType` and touch nothing; the caller keeps the `Value` subtree and writes it back unchanged (ADR-0077 §6, `UnknownComponents` in the scenes spec).
- **References and entities are strings handed to the caller** (ADR-0076 §2, ADR-0078 §3). `Resolvers` holds four callbacks: `readReference(path, FieldInfo, void* handle)` and `writeReference(const void* handle, FieldInfo) -> std::string` for `reference` fields, `readEntity(id, FieldInfo, void* entity)` and `writeEntity(const void* entity, FieldInfo) -> std::string` for `entity` fields. The bridge writes an empty string from a resolver as `null`, reads `null` as the kind's empty value without a diagnostic, and never knows an asset path's meaning, a store or an id map. A `path` field is a `String` with no resolver. A missing resolver leaves the field empty on read and writes `null`, counted in `unresolved`.
- **`BridgeResult`** is `status` (`Succeeded`, `Partial`, `UnknownType`), the counts `defaulted`, `unknownFields`, `unresolved` and the `Diagnostics`, each logged once; `Partial` whenever a count is non-zero.

### Upgraders

- **`Upgrader`** is `void(std::uint32_t fromVersion, Value& tree)` (ADR-0077 §6). **`RegisterUpgrader(TypeRegistry&, typeName, fromVersion, Upgrader)`** stores it in the type's type-erased upgrader slot (the types spec's; `FenrirTypes` still knows no tree); the C# side registers through the same door (the csharp spec).
- **`Upgrade(const TypeRegistry&, typeName, std::uint32_t fromVersion, Value& tree, ILogger&) -> UpgradeResult`** runs the upgraders from `fromVersion` to the type's `@version` in order, each stepping one version. A missing step is one error and the tree is left at the version reached; a tree newer than the type is read as it is with one warning (no downgrade). The caller passes `fromVersion` from wherever it read it (the envelope's `version`, the data-assets spec), so the package never names an envelope key. Cooked trees run upgraders too (ADR-0085 §2).

### The tree encoding and `ReadTree`

- **`EncodeTree(const Value&, Trivia) -> std::vector<std::byte>`** and **`DecodeTree(std::span<const std::byte>, path, ILogger&) -> ReadTreeResult`** are the compact binary form of a value tree (ADR-0083 §2, glossary "Tree encoding"): a four-byte tag whose first byte is zero, a version byte, then one node per tag byte with LEB128 lengths and counts, little-endian numbers in their own width, length-prefixed UTF-8 strings and keys, members in order, and trivia as its own node kind so `DecodeTree(EncodeTree(t, Trivia::Keep))` equals `t` with trivia; `Trivia::Drop` is what the C# pull and a cooker ask for. No string table yet; the cooking effort measures whether one pays.
- **`ReadTree(std::span<const std::byte>, std::string_view path, ILogger&) -> ReadTreeResult`** is the one door loaders call (ADR-0085 §2): a first byte of zero is the encoding, anything else is UTF-8 text (a BOM skipped), so `FenrirAssets` never knows which it got.
- **No envelope helper.** `format`, `type`, `version`, `data` and `sub` are `FenrirAssets`' keys (ADR-0079 §1, the data-assets spec); a game's save file composes `ReadTree`, `WriteText`, `ToTree` and `FromTree` with keys of its own (ADR-0079 consequences).

### Testing

Tests cross the package's public seams only, on hand-registered types in a test-only registry (ADR-0079 §7), with a `RecordingLogger` as `AssetsTest::RecordingLogger` in `packages/FenrirAssets/Tests/AssetsTest.hpp`, Catch2 through `fenrir_add_catch2()`, and fixtures under `packages/FenrirSerialisation/Tests/fixtures/` named by a `FENRIR_SERIALISATION_FIXTURES` definition as `FENRIR_ASSETS_FIXTURES` is. No `App`, no window.
- **Golden files:** the research's `sword.asset.json5` and `level1.scene.json5` (the JSON5 samples of `research/format-fit`, 688 and 1,451 bytes), committed as fixtures and never regenerated. Three tests each: read then `WriteText` gives the same bytes; a hand-built tree writes the same bytes; a hand-registered `MyGame::Sword` (with its `MyGame::ParticleEffect` sub-asset) and test components named as the scene's (`Fenrir::Name`, `Fenrir::Transform`, `Fenrir::MeshRenderer`, `MyGame::BoneAttachment`, `Fenrir::DirectionalLight` with the sample's `color` field) through `ToTree` write the same bytes under the envelope the test builds by hand.
- **The reader:** a committed copy of the `json5-tests` cases (MIT, with its licence) under `fixtures/json5-tests/`: every valid case parses, every invalid case fails with a diagnostic whose line and column are asserted from the case; duplicate keys, the depth limit, a BOM and CRLF each have a test.
- **The writer:** one test per emit rule (quoted and bare keys, each escape, the flow rule's four branches, each number width, `Infinity` and `NaN`), and a determinism test writing the same tree twice.
- **The bridge:** a round trip struct to tree to struct for every kind; one test per tolerance rule asserting the count, the default taken and the diagnostic's position; `@formerly` both ways; an unknown type kept and written back unchanged; resolver calls observed through recording lambdas.
- **Upgraders:** `sword` at `version: 1` with a renamed field reaches version 2 through two registered upgraders in order; a missing step and a newer tree each produce their one diagnostic.
- **The encoding and `ReadTree`:** both goldens encoded and decoded equal their trees, with and without trivia; `ReadTree` on the text bytes and on the encoded bytes gives equal trees; the encoding of each golden is smaller than its text.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | The package, `Value` and `WriteText`: hand-built sword and scene trees write the golden bytes | — |
| 02 | `ReadText`: the full grammar, located diagnostics, the `json5-tests` fixtures, goldens round-trip with comments | 01 |
| 03 | `ToTree` over the registry's visitor: declaration order, defaults omitted, every kind's tree form | 02 (+ types spec) |
| 04 | `FromTree`: the tolerant read, `@formerly`, unknown fields and types, `BridgeResult` | 03 |
| 05 | `Resolvers`: `reference` and `entity` fields as strings, `null` for none, `path` fields | 04 |
| 06 | `Upgrader`, `RegisterUpgrader` and `Upgrade` in order, no downgrade | 04 |
| 07 | Comments survive a value rewrite: `ToTree` with a `prior` tree | 05, 06 |
| 08 | `EncodeTree` and `DecodeTree`, with and without trivia | 02 |
| 09 | `ReadTree` sniffing text or binary from a byte span | 07, 08 |

Every ticket touches `packages/FenrirSerialisation/` and its `Tests/CMakeLists.txt`, so they stack: 01, 02, 03, 04, 05, 06, 07, then 09. Ticket 08 touches only the new `Encoding` files and one line of `Tests/CMakeLists.txt`, so it is cut from 02 and runs beside the bridge chain; 09 is cut from 07 with 08 merged in. Tickets 01, 02 and 08 compile without `FenrirTypes` and may land before the types spec; 03 onwards wait for its registry, visitor, kinds and upgrader slot.

## Out of scope

The types spec: the registry, the visitor, the kinds, `@version`, `@formerly`, the generator and the CI check. The data-assets spec: the envelope and its keys, `AssetKind::Data`, every store door, `LoadResult`'s `diagnostics` list, sub-assets, reference checking and assignability, path-from-handle, the move tool, the writer's path check and the `AssetSource` seam (ADR-0085 §1 and §3), the asset watcher. The scenes spec: the entity layout rule, `EntityId`, the id map, `UnknownComponents`, `Transient`, instances, the scene patch. The csharp spec: `IDataAsset`, the codecs, the generation cache and the tree natives over `EncodeTree`. The dev-time spec: the project file, its layers and import sidecars. From the map's fog and out-of-scope lists: a semantic merge driver, the cooked pack format and cooker, a byte-image fast path for fixed-layout components (ADR-0085 §2), a string table in the encoding, binary blobs in text files (ADR-0077 §8), a strict-JSON export switch, syntax-error recovery, a schema or validation tool, asynchronous loading, text in data assets, game save file conveniences.

## Gaps settled here

- **Syntax errors are not tolerated; fields are.** ADR-0077 §7's "partly broken file loads the parts that read" is met at the bridge (a bad field takes its default, one diagnostic, counted), not by recovering from broken syntax, which the research found no format or library does. A duplicate key is a warning with the first value kept, not a refusal.
- **The flow-style rule is sharpened to match the goldens:** flow for an array of scalars, and for an all-scalar object that is an array element or has one member; ADR-0077's "single-field objects" alone would write `sub: {sparks: {...}}` and the sword's table rows differently from the samples the ADR names as goldens.
- **A `Number` has four widths** (signed, unsigned, single, double) so `std::to_chars(float)` reproduces `0.7071068` and a 64-bit integer survives; the bridge sets the width from the field's kind.
- **Trivia is leading comments, one trailing comment and closing comments**, kept with delimiters, blank lines dropped; a value rewrite keeps comments by key path through `ToTree`'s `prior` tree, since a struct holds none.
- **Tree forms nobody decided:** a tagged union is an object whose first member is `kind`; a bound type uses the shape the types spec registers (an array of N scalars); a disengaged `optional` is omitted and `null` reads as disengaged; `null` in any other field is a diagnostic and the default, except `reference` and `entity`, whose `null` is "none" (ADR-0078 §3).
- **Upgraders register through a type-erased slot on the registry** so `FenrirTypes` stays tree-free; the slot is listed as a prerequisite on the types spec. A newer-than-type tree reads as it is with one warning; a missing step stops at the version reached with one error.
- **The encoding starts with a zero byte**, which no JSON5 text can, so `ReadTree` sniffs one byte; it carries trivia optionally and has no string table.
- **No envelope helper**, although the brief allowed one: the keys stay `FenrirAssets`' alone, and a game's save file picks its own.
