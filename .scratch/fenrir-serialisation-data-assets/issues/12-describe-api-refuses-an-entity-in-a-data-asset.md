# 12: describe-api refuses an `entity` reached from a `data-asset` type

**What to build:** ADR-0075's "a data asset never holds an `Entity`" is enforced when a description is generated, so the store never meets one. The types spec allowed `entity` in every role only because it is fixed-layout, and no spec uses one in a data asset. The user decided to refuse it on 2026-11-24 (the spec, "Gaps settled here").

- **The refusal:** describe-api fails when a `data-asset` type reaches an `entity`. The failure names the data asset type, the field and the path that reaches it (for example `MyGame::Spawner.slots[].target`).
  - A reach can run through any nested struct, a `list` or `optional` element, an array, or a base.
  - It also runs through a component held by value. A data asset therefore cannot hold a component with an `Entity` field by value, as ADR-0075 asks.
  - An `entity` in any other role is unchanged: components, scene resources, shared structs and `settings` keep it.
- **Docs and tests:**
  - `SCHEMA.md` says the rule in "Owning kinds" and in the `entity` kind's row, and Iris's copy matches.
  - `Failing.hpp` gains a data asset with a direct `entity` field, one that reaches an entity through a nested struct, and one holding an entity-holding component by value, each with its diagnostic.
  - The fixture's existing types stay valid.
- **Regeneration:** check that every package's description still regenerates unchanged. No engine data asset type exists yet.

**Blocked by:** none.

**Touches:** `tools/describe-api/src/` (`Describer.cs`), `tools/describe-api/SCHEMA.md`, `tools/describe-api/tests/` (`Failing.hpp`, `DescribeTests.cs`), `packages/Iris/docs/api-description.md`

**Status:** resolved

- [x] A data asset with an `entity` field fails, naming the type, the field and the path.
- [x] A data asset reaching an `entity` through a nested struct, a list element or a component held by value fails the same way.
- [x] Components, scene resources and shared structs with `entity` fields describe as before, and every package's description regenerates byte-identical.
- [x] `SCHEMA.md` and Iris's copy state the rule.
