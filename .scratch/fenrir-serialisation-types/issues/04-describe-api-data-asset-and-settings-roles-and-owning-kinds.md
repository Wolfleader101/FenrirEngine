# 04: describe-api: the `data-asset` and `settings` roles, the owning kinds, the base type and the per-role refusals; Iris.Bindings skips them

**What to build:** a C++ struct holding `std::string`, `std::vector<T>`, `std::optional<T>` or `std::filesystem::path` can be described when its selection gives it the role `data-asset` (ADR-0075) or `settings` (ADR-0084), and a `data-asset` type may extend one other `data-asset` type, with data members in bases allowed there alone. Components, events, resources and shared structs keep every refusal they have, now naming the role in the diagnostic. Iris.Bindings skips the two roles and the owning kinds wherever it meets them, so the dirty-tree check passes between this ticket and the C# spec.

- **Roles:** `data-asset` and `settings` join the selection's roles and the description's `role` values. A `data-asset` records `base` (the canonical name of the one type it extends, or absent); extending a type that is not a `data-asset`, extending two, or a virtual function fails the tool. A `settings` type records no base.
- **Owning kinds:** `string` (`std::string`), `list` (`std::vector<T>`, with `elementKind` and `elementType` as an array has, the element any kind the role allows, `list` included), `optional` (`std::optional<T>`, any kind but `optional`), `path` (`std::filesystem::path`); recognised by qualified name. They record no `offset` (ADR-0074 "without using their offsets") and the type records no `size` or `alignment` requirement beyond what the compiler says, since a generated visitor reaches them by member, not by byte.
- **Nested values:** a described struct by value inside an owning type may itself hold owning kinds when it is reached only from `data-asset` and `settings` types; the same struct reached from a component keeps the fixed-layout rule, and a struct reached from both fails naming the two paths.
- **Refusals by role:** a `string`, `list`, `optional` or `path` in a `component`, `event`, `scene-resource`, `app-resource` or `shared-struct` fails naming the type, member and role; a data member in a component's base still fails; the trivially-copyable rule is lifted for the two new roles only.
- **Defaults:** a `string` or `path` default is its initialiser's literal; a `list`, `optional` and `reference` default is empty and recorded as such; the "Kinds" default table in `SCHEMA.md` says so.
- **Iris.Bindings** skips every `data-asset` and `settings` type and every field of an owning kind without failing, logging nothing, and fails as today on an owning kind in any other role (which the tool already refuses).

**Blocked by:** 03.

**Touches:** `tools/describe-api/src/` (`Selection.cs`, `Describer.cs`, `Description.cs`, `DescriptionWriter.cs`), `tools/describe-api/SCHEMA.md`, `tools/describe-api/README.md`, `tools/describe-api/tests/`, `packages/Iris/Tools/Iris.Bindings/` (the role and kind skip, its tests)

**Status:** ready-for-agent

- [ ] The fixture header gains a `data-asset` `WeaponData` with a `string`, a `list` of a nested struct, an `optional<float>` and a `reference`, a `Sword` extending it with members of its own, and a `settings` type with a `path` and a `list` of strings; `Fixture.api.json` records the roles, `base`, the kinds without offsets, compared byte for byte.
- [ ] `Failing.hpp` gains a component with a `std::string`, a `data-asset` with a virtual destructor, one extending a `shared-struct`, and a nested struct reached from both a component and a data asset, each failing with its diagnostic.
- [ ] Iris.Bindings regenerates the fixture copy's bindings with the new types absent and the existing output byte-identical; its tests cover the skip.
- [ ] Every package's description and binding regenerates byte-identical (no engine header is in either role yet).
- [ ] `SCHEMA.md` documents both roles, the four kinds, `base`, the nested-value rule and the per-role refusals.
