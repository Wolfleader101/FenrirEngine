# 05: The report and admission carry data asset types all or nothing

**What to build:** a C# data asset type is in the engine's type registry while its script generation runs, with its upgraders, and is gone again on `Forget` (ADR-0083, ADR-0077). See the spec's "The script report and admission".

- **Reporting.** `DataAssetTable.Register` reports each entry through `ScriptDataAssets.Report<T>` as a tree in the tree encoding. `ScriptReport` gains `dataAssets`, with bases ordered first.
- **Admission.** Data asset entries are admitted all or nothing with the rest of the report. Each of these refuses the whole load, leaving the registry as `Forget` would:
  - a canonical name that isn't unique against the registry and the report;
  - a base that isn't a registered data asset type (a C++ one, or a C# one earlier in the report);
  - a kind the `data-asset` role doesn't allow;
  - a `reference` target that is neither registered nor an asset kind;
  - upgrader versions that aren't consecutive below the type's version.
- **Registration.** Each entry is registered as a tree-backed `data-asset` description (ticket 01) in the generation's arena.
  - Its upgraders are bound by name. Each `fromVersion` the type covers registers an upgrader that calls the table's `Upgrade(name, fromVersion, bytes)`, as the system host's methods are bound.
- **`Forget`.**
  - It removes the upgraders, then unregisters the types, topmost first.
  - It asks the store's seam from ticket 02 first. A type that live records hold is not unregistered; it is kept, with a warning, until the records go.
  - A shape change on reload unregisters the old type and registers the new one.
- **Rules tests.** `Tests/Rules/ScriptDataTests.cpp` admits literal reports with data asset entries, one for each refusal above, and checks that each leaves nothing.

**Blocked by:** 04.

**Touches:** `packages/FenrirScripting/Managed/` (`Fenrir.Scripting/ScriptDataAssets.cs`, the table's `Upgrade`), `packages/FenrirScripting/src/` (`ScriptComponents.hpp`, `ScriptData.*`, the binding by name), `packages/FenrirScripting/Tests/` (`Rules/ScriptDataTests.cpp`, the end-to-end tests pinning `GeneratedNames`)

**Status:** ready-for-agent

- [ ] A fixture script's C# data asset types are in the registry after admission, a subtype `IsA` its C# base, and its upgraders are found by `Upgrade`.
- [ ] Each refusal case refuses the whole report and leaves the registry unchanged.
- [ ] `Forget` removes the types and upgraders, and a reload with a changed shape registers the new shape.
