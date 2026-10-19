# 01: Read-only fields through Ref<T>

**What to build:** A C# system holding `Ref<T>` to a writable component can read that component's `readonly` fields in place, and cannot write them (spec: "Prerequisite: read-only fields through `Ref<T>` (FenrirScripting)"). For each `readonly` field of a writable component, `ComponentFieldsGenerator` also emits a `ref readonly` accessor on `Ref<T>`, beside the one it already emits on `ReadOnlyRef<T>`. A controller holding `Ref<CharacterBody>` to write `Velocity` will read `body.State.Ground` through the same reference, with no second lookup.

This is FenrirScripting work, independent of physics, and lands before any character scripting. Any later component with a read-only part gets it for free.

- Rejected: a writable `State` as in C++ (loses the guard), and documenting `body.Value.State` as the way to read it.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] Generator tests: a `readonly` field of a writable component gets a `ref readonly` accessor on `Ref<T>`, and the existing `ReadOnlyRef<T>` accessor is unchanged.
- [x] Generator tests: writing that field through `Ref<T>` does not compile (the generator's diagnostics or a compile-failure case).
- [x] `ScriptingTests`: a test component with a `readonly` field written by C++ reads back through `Ref<T>` in a C# system, in place.
- [x] Writable fields of the same component are still writable through `Ref<T>`; components with no `readonly` fields generate exactly what they did before.
