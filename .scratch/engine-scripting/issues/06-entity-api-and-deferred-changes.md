# 06: Entity API and deferred structural changes

**What to build:** `Entity` helpers `Get<T>` (returns `Ref<T>`), `Has<T>`, `Add<T>(value)`, `Remove<T>`, `Destroy`, and `ctx.CreateEntity()`, working for native and C# components alike on the active scene. Structural changes made while a C# system runs (create, destroy, add, remove) go into a buffer and are applied in order when the system returns. `Get` and `Has` read live state. Outside a system (for example `OnCreate`), changes apply at the same playback point.

**Blocked by:** 04, 05

**Status:** ready-for-agent

- [ ] Every `Ref<T>` taken during a system stays valid until the system returns, even when other entities of the same type gain or lose that component in the same system.
- [ ] A created entity's ID is usable within the system (for example to add components), and it exists afterwards.
- [ ] `Get<T>` on a missing component or a stale entity fails clearly (exception with a message) and never returns a dangling `Ref<T>`.
- [ ] Changes are applied in the order they were recorded, and destroy-then-add on the same entity is well defined and tested.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Access).
