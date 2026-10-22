# 21: FenrirScripting: native resources and the change buffer on the call context

**What to build:** Scripting extensions can mirror engine resources to C# and their native functions can make structural changes, the two capabilities every renderer-stack extension needs (spec: "Scripting: the FenrirScripting prerequisite" (native resources, facades, the change buffer, README, ADR-0009), "Testing Decisions" seam 7 (native resources, the change buffer); ADR-0029, ADR-0009). `Fenrir.Math` is ticket 22.

- **Native resources:** an extension registers `MakeSceneResource<T>("Fenrir.X.Name")` or `MakeAppResource<T>(…)` (ticket 01's app resources), with read-only variants. C# declares a blittable struct with `[NativeResource("…")]`; the layout is checked at load and a mismatch fails the load.
- **`Resources.Get<T>()`** returns a `Ref<T>` (or `ReadOnlyRef<T>`) into engine storage, the active scene's for a scene resource; `Resources.TryGet<T>(out …)` covers absence; `Get` on an absent resource logs and returns an empty ref. Inserting and removing a resource are deferred structural changes. Access works from any C# code.
- **Facades:** documented as the pattern for a resource with behaviour (a static C# class of native functions), so C++'s rules are never re-implemented.
- **The change buffer on the call context:** an extension's native function may create and destroy entities, add and remove components, and insert and remove resources; a created entity is usable at once, the rest plays back in order when native code regains control. Direct structural changes stay forbidden.
- **ADR-0009 amended**; the FenrirScripting README's extension sections rewritten for native resources, facades and the change buffer.

**Blocked by:** 01 (Engine prerequisites).

**Touches:** `packages/FenrirScripting/`, `docs/adr/0009-engine-packages-extend-scripting-through-extensions.md`

**Status:** ready-for-agent

- [ ] Scripting harness tests: a test extension's scene and app resources are registered, read and written from C# through `Ref<T>`, and a read-only one rejects writes at compile time (a compile-fail fixture); a layout mismatch fails the load with a diagnostic.
- [ ] `TryGet` is false when absent; `Get` on an absent resource logs and returns an empty ref; a scene resource follows the active scene.
- [ ] Insert and remove from C# take effect after the system, in order.
- [ ] A test extension's native function creates an entity that is usable at once in the same call (a component added to it, its id returned), and destroys, adds and removes play back in order after the call.
- [ ] Existing scripting and physics scripting tests still pass; ADR-0009 and the README are amended.
