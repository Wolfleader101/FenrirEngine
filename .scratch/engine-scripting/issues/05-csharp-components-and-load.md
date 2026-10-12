# 05: C# components and all-or-nothing loading

**What to build:** C#-defined components with no C++ code. An EnTT pool class for opaque fixed-size bytes is registered once per C# component type under a named storage id (EnTT 3.13 `storage_type` specialisation or equivalent), so it takes part in `registry.destroy` and runtime views. On load, the engine finds `unmanaged` `IComponent` structs, and the managed side reports each one's full name, size and alignment through an internal call. Loading follows the spec's all-or-nothing order: components, then systems (from 08 onward), then mapping validation. Any failure rolls back every registration from that load.

**Blocked by:** 03, Iris 20

**Status:** ready-for-agent

- [ ] A C# component added to an entity is removed when the entity is destroyed.
- [ ] A runtime view over a native pool and a C# pool iterates the right entities.
- [ ] Alignment is honoured, and storage pages don't move existing elements on growth.
- [ ] A load that fails partway leaves no new pools or registry entries, and the previous generation (if any) is untouched.
- [ ] A non-`unmanaged` `IComponent` is rejected at compile time (generator or constraint) or at load with a diagnostic.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Model; Loading). C# component data lives in native memory, which ticket 10 relies on.
