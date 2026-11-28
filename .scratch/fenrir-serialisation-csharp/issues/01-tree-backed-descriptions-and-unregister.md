# 01: FenrirTypes: tree-backed descriptions, `TypeRegistry::Unregister`, and upgrader removal

**What to build:** the registry can hold a type whose layout C++ does not know, and can remove a type again. This is what a C# data asset type needs: its fields are only known at run time, and a script reload changes its shape. The user decided it on 2026-11-28; see the spec's "The registry and the store" and "Gaps settled here".

- **A tree-backed `TypeDescription`.**
  - It has no `construct`, `destroy` or `visit`, and no `size`.
  - Its fields carry names, kinds, targets and metadata, but no offsets.
  - Its default is a value tree.
  - A flag marks it, so code that walks layouts can tell it apart.
  - `Register` accepts it with owning-kind fields, and `IsA` and `base` work as for any data asset type. A tree-backed type may extend a C++ data asset type.
  - The registrant owns the description's storage. The registry stays non-owning.
- **`TypeRegistry::Unregister(name)`.**
  - It removes the type and every attachment hung on it.
  - It is refused, with a reason, while another registered type names it as its base.
  - It is not for engine types: the doc says so, and nothing guards it beyond the base check.
- **Upgrader removal.** FenrirSerialisation gains a way to remove a type's upgraders, beside `RegisterUpgrader`.
- **The bridge.** `ToTree` and `FromTree` never treat a tree-backed type's description as a layout. They refuse to walk one, with a diagnostic, because its value is a tree already.

**Blocked by:** none.

**Touches:** `packages/FenrirTypes/` (`include/FenrirTypes/TypeDescription.hpp`, `TypeRegistry.hpp`, `src/`, `Tests/`), `packages/FenrirSerialisation/` (`include/FenrirSerialisation/Upgrade.hpp`, `src/Upgrade.cpp`, the bridge's guard, `Tests/`)

**Status:** resolved

- [x] A tree-backed type with `string`, `list` and `reference` fields registers, is found, and `IsA` its C++ base.
- [x] `Unregister` removes a type and its attachments. It is refused while a registered type names the type as its base.
- [x] A type's upgraders can be removed, and registered again after.
- [x] The bridge refuses to walk a tree-backed description, with a diagnostic.
