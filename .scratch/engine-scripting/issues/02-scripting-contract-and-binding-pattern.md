# 02: Fenrir.Scripting contract, Entity, and the binding pattern

**What to build:** The shared contract assembly `Fenrir.Scripting`, registered as an Iris shared assembly and never reloaded (ADR-0004). It establishes the one pattern every engine API uses: a C++ function registered with `RegisterInternalCall`, plus a friendly static C# wrapper in the contract that hides pointers, UTF-8 and lengths from authors (review-18 E4). This ticket proves the pattern with `Log` (Info/Warn/Error to `ILogger`), `Time` (delta time, tick rate) and the `Entity` value type (`readonly struct Entity { uint Value; }`, the full `entt::entity` value including version bits, with `IsValid` against the active scene).

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] A fixture script assembly referencing `Fenrir.Scripting` logs through `Log` and reads `Time`, and the test target observes both.
- [ ] `Entity.IsValid` is false for a destroyed entity, and false again after its ID is reused (the version differs).
- [ ] Contract types have one identity across generations (they come from the shared assembly).
- [ ] The binding pattern is documented in the package README as the recipe for adding an engine API.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Binding native code; Model).
