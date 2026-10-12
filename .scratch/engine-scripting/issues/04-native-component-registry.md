# 04: Native component registry

**What to build:** The runtime registry that maps a component name to EnTT storage and its operations (get pointer, has, add, remove), since EnTT has no runtime type map. `scripting.RegisterComponent<T>("Fenrir.Transform")` registers a C++ component with an Iris `StructMapping`. On every script-assembly load, each registered mapping is validated explicitly against its C# struct (Iris 21), and any mismatch fails the load. Register the engine's `Transform` and `Name` as `IComponent` structs in the contract, and expose `Relationship` (hierarchy) read-only.

**Blocked by:** 03, Iris 20, Iris 21

**Status:** ready-for-agent

- [ ] A C# `Ref<Transform>` from the registry reads and writes the same memory C++ sees through `GetComponent<Transform>`.
- [ ] A deliberately mismatched C# struct fails the load, with a diagnostic naming the field.
- [ ] `Name` (fixed-size text) round-trips both ways.
- [ ] Writes to `Relationship` are impossible from C#.
- [ ] A user component registered from outside FenrirScripting (in the test target) works with no engine changes.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Binding native code).
