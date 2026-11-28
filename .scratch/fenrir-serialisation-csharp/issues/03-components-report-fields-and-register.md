# 03: Components report fields, and admission registers C# components with the registry and `ComponentType::Dynamic`

**What to build:** a C# component is described like a native one, so the scene writer and reader write it by canonical name (ADR-0083, ADR-0074; the spec's "The script report and admission").

- **The field list.** `ComponentTable`'s entry gains a field list, and each field carries:
  - its name, kind and target;
  - `Entity` as `entity`;
  - `AssetHandle<T>` as `reference`, with its target;
  - a `[FixedString]` `fixed byte` buffer or `[InlineArray]` byte struct as `fixed-string`;
  - a `[Transient]` field flagged.
- **How the walk works.**
  - It includes fixed buffers and only public fields.
  - Offsets are measured at run time, as `ScriptComponents.AlignmentOf` measures alignment.
  - A `string` or `List<T>` field stays refused (`FENRIR0004`).
  - `[Transient]` and `[EditorOnly]` on the type mirror the commands.
- **New attributes** in `Fenrir.Scripting`: `[FixedString]`, `[Transient]` (type and field) and `[EditorOnly]`.
- **`ScriptReport` and `ScriptData`.**
  - `ScriptReport` gains per-component fields.
  - `ScriptData` gains the App's `TypeRegistry`.
- **Admission.** For every C# component, admission:
  - registers a fixed-layout description, with fields, kinds, offsets and a `new T()` default, into an arena owned by the script generation;
  - attaches `ComponentType::Dynamic`.

  `Forget` unregisters them through ticket 01's `Unregister`. Admission stays all or nothing.
- **Fingerprint.** The reload fingerprint keeps working, from Iris's fields as now.

**Blocked by:** 01.

**Touches:** `packages/FenrirScripting/Managed/` (`Fenrir.Scripting/` attributes and `ScriptComponents.cs`, `Fenrir.Scripting.Generators/ComponentFieldsGenerator.cs`), `packages/FenrirScripting/src/` (`ScriptComponents.hpp`, `ScriptData.*`), `packages/FenrirScripting/Tests/`

**Status:** ready-for-agent

- [ ] A fixture component with an `Entity`, an `AssetHandle<T>`, a `[FixedString]` buffer and a `[Transient]` field reports all four, with the right kinds and offsets.
- [ ] After admission, `ComponentType::ForName` finds it, and FenrirAssets' scene writer saves it by canonical name with its transient field left out.
- [ ] `Forget` unregisters it, and a reload registers it again.
- [ ] A `string` field in a component is still refused at compile time.
