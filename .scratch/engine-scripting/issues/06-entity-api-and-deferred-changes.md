# 06: Entity API and deferred structural changes

**What to build:** `Entity` helpers `Get<T>` (returns `Ref<T>`), `Has<T>`, `Add<T>(value)`, `Remove<T>`, `Destroy`, and `ctx.CreateEntity()`, working for native and C# components alike on the active scene. Structural changes made while a C# system runs (create, destroy, add, remove) go into a buffer and are applied in order when the system returns. `Get` and `Has` read live state. Outside a system (for example `OnCreate`), changes apply at the same playback point.

**Blocked by:** 04, 05

**Status:** resolved

- [x] Every `Ref<T>` taken during a system stays valid until the system returns, even when other entities of the same type gain or lose that component in the same system.
- [x] A created entity's ID is usable within the system (for example to add components), and it exists afterwards.
- [x] `Get<T>` on a missing component or a stale entity fails clearly (exception with a message) and never returns a dangling `Ref<T>`.
- [x] Changes are applied in the order they were recorded, and destroy-then-add on the same entity is well defined and tested.

## Comments

### Resolution

- `Entity.Create()`, `Destroy()`, `Add<T>()`, `Add<T>(value)`, `Remove<T>()` in the contract; `Has`, `Get` and `IsValid` unchanged (live). `ctx.CreateEntity()` will forward to `Entity.Create()` when the `SystemContext` exists (ticket 08); there is no context type yet.
- Native `ChangeBuffer` (`src/ChangeBuffer.*`) records changes in order and plays them back. A created entity's id comes from the new `EntityList::ReserveEntity()` (alive, no components) and is completed by `CompleteEntity()` at playback. Four new internal calls (`EntityCreate`, `EntityDestroy`, `ComponentAdd`, `ComponentRemove`) return a refusal code that C# turns into an exception.
- The playback point is `Scripting::ApplyChanges()`, called by whatever calls into a script. Nothing in the engine calls it yet: the runner (ticket 08) will, after every system and after `OnCreate`/`OnDestroy`, which is what makes "outside a system" behave the same. Until then the tests call it by hand, so "applied when the system returns" is proven for the mechanism, not yet for a real system. `DiscardChanges()` is there for a system that throws; whether the runner discards or applies then is ticket 08's decision.
- Decisions the ticket left open: `Add(value)` replaces an existing component, `Add<T>()` keeps it; a change to an entity destroyed earlier in the same batch is skipped with a warning (destroy-then-add leaves it destroyed); Transform, Name and Relationship are `required` and cannot be removed.
- Not done here: `ctx.CreateEntity()`, "visible from the next system on" and event sends (tickets 08/09). Not tested: native code destroying an entity between recording and playback.
- Limits: a change recorded against an entity that was alive when recorded but destroyed by *native* code before playback is skipped the same way. Changes are not applied if nobody calls `ApplyChanges()`.

Spec: `.scratch/engine-scripting/spec.md` (Access).
