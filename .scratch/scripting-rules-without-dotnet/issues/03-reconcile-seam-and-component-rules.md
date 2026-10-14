# 03: Reconcile over what the assembly reports: the seam and the component rules

**What to build:** A maintainer can check what is refused when a script assembly reports its components without an assembly that lies (spec: "Seam 2").

Reconcile gets a narrow interface for the assembly side, asked in today's order: the components the assembly defines, the events it defines, a layout fingerprint for a definition, and whether the native components and events match their C# structs. Two adapters stand behind it: one over a loaded Iris component (today's code, moved) and one that returns literals for tests. The order of the work and every message stay as they are. This ticket introduces the seam and moves the component rules onto it; events and stale data follow in the next ticket.

**Blocked by:** 01 (Rules test executable, change buffer cases and the probe).

**Status:** resolved

- [x] Reconcile talks to the assembly only through the new interface. The Iris-backed adapter does what the old code did, in the same order, with the same messages.
- [x] Cases in the rules executable, with the literal adapter, pin the component refusals: a component reported twice (message unchanged); a layout no C# struct has, for zero size, an alignment that is not a power of two and a size that is not a multiple of the alignment; a script component named like a native one; two script components whose names share a storage id, naming both.
- [x] A case pins that a refusal leaves the registry unchanged and no storage made, and one that a sound report of components registers them all.
- [x] The matching C#-driven cases are removed or cut down to what needs C#. The end-to-end cases that load real assemblies stay and pass unchanged.
- [x] The full CTest suite passes.

## Comments

Done. The assembly side is `ScriptAssembly`: report one kind's definitions, fingerprint a definition, check the native components and events against their C# structs. `Reconcile` takes an App, the registry, a `ScriptAssembly` and the owned data; the Iris-backed adapter carries the old table-running, fingerprint and validation code, and the previous `Reconcile(context, registry, component, owned)` builds it and delegates, so the lifecycle's hook is unchanged. The order of the questions is today's: components, events, native check, then fingerprints for what is kept.

Moved off C#: "a script component cannot take a native component's name" and "a script component with an impossible layout is refused", with the `bad-component-layout` bad-report mode. Kept end to end: the duplicate component (a real assembly through the generated table and native function), the failed-load rollback, and everything that loads real assemblies. A mutation (the name-clash rule removed) fails its case.
