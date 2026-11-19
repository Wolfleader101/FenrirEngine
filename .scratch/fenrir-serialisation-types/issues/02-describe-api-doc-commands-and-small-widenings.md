# 02: describe-api: type and field doc commands, `[registration]` accepted, bound-type components, constant-expression defaults

**What to build:** `tools/describe-api` records what files and an inspector need from a type's doc comments and selection without changing any existing description's meaning: `@version`, `@formerly`, `@transient` and `@editor-only` on a type, `@formerly` and the metadata commands on a field, a `[registration]` table in the `.api.toml` it accepts and leaves to the registry generator, a bound type's components, and defaults from constant expressions. Every committed `.api.json` is regenerated and differs only where a header already carries one of these (none does yet, so the diff is additive keys with default values or nothing at all). This is the prefactoring ticket: tickets 03 and 04 add kinds and roles on top of it.

- **Doc commands** are split out of the doc text as the natives' `@param` and `@facade` already are (`DocComment.cs`): on a type, `@version N` (default 1, below 1 fails), `@formerly Old::Name` (repeatable), `@transient`, `@editor-only`; on a field, `@formerly oldName` (repeatable), `@range min max`, `@step s`, `@display text`, `@group text`, `@readonly`, `@hidden`, `@multiline`, `@units text`. A command on the wrong subject, a malformed one or an unknown `@` word fails the tool naming the type, member and command, as a bad `@param` does today.
- **The description** records `version`, `formerly` (a list), `transient` and `editorOnly` on a type, and `formerly` and a `metadata` object (only the commands present) on a field; the `doc` string no longer contains the commands. `SCHEMA.md` gains the keys and the command table; Iris's copy of the schema document is kept equal.
- **`[registration]`** in a `.api.toml` is parsed and ignored by `Selection.cs` with a note that `tools/generate-type-registry` reads it (ticket 05); an unknown table still fails as today.
- **Bound-type components:** the scalar fields the tool already reads to match a bound type's constructor are recorded as `components` (name, kind, offset) on the bound type, so a visitor can present a `Vec3` as three `f32` and a `Mat4` as sixteen; `Fenrir.Math`'s and physics's bound types regenerate with the new key and Iris.Bindings ignores it.
- **Defaults:** a scalar's default member initialiser may be any constant expression libclang evaluates (a named `constexpr`, an arithmetic expression), not only a literal, so `RendererConfig::debugValidation`'s `DebugValidationByDefault` describes without a spelling change (ADR-0043 changed spellings instead); an expression libclang cannot evaluate fails as an unsupported default does today.
- **Iris.Bindings** reads every regenerated description unchanged in behaviour: the generated bindings' bytes do not move, which the dirty-tree check proves.

**Blocked by:** none.

**Touches:** `tools/describe-api/src/` (`DocComment.cs`, `Describer.cs`, `Description.cs`, `DescriptionWriter.cs`, `Selection.cs`), `tools/describe-api/SCHEMA.md`, `tools/describe-api/README.md`, `tools/describe-api/tests/` (the fixture header, `Fixture.api.json`, `Failing.hpp`, `DescribeTests.cs`), every `packages/*/api/*.api.json`, `packages/Iris/Tools/Iris.Bindings/` (the schema copy and its fixture description)

**Status:** ready-for-agent

- [ ] The fixture header gains a type with `@version 2`, two `@formerly` names, `@transient` and `@editor-only` types, and a field carrying each metadata command; the committed `Fixture.api.json` is regenerated and compared byte for byte in the tests, and the `doc` strings carry no `@` commands.
- [ ] `Failing.hpp` gains a malformed `@range`, a `@version 0`, a field-level `@version` and an unknown command, each failing with a diagnostic naming the type, member and command.
- [ ] A bound type in the fixture records `components` with the offsets the tests assert.
- [ ] A default written as a named `constexpr` and one written as `2 * 4` describe as their values.
- [ ] `fenrir-describe-api` regenerates every package's description with no change but the new keys, and `fenrir-generate-bindings` leaves every binding byte-identical; both are committed.
- [ ] `SCHEMA.md` documents every new key and command, and Iris.Bindings' copy matches it.
