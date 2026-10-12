---
status: accepted
---

# C# scripting is ECS-first, not per-entity behaviour objects

The engine's C++ side puts data in components and logic in systems. C# scripting follows the same model: C# components are `unmanaged` `IComponent` structs stored in EnTT next to native components, and C# systems are `ISystem` classes scheduled into the same frame phases. There are no Unity-style per-entity behaviour objects. We chose this so that C# keeps the ECS's data layout and speed, C++ and C# share one mental model and can query each other's components, and component data lives in native memory, where it survives a script reload.

## Considered options

- **Per-entity behaviour objects (Unity MonoBehaviour, Godot node scripts).** These were rejected as the foundation. They mean one managed object per entity, GC pressure, a second programming model beside the C++ systems, and rooted objects to tear down on every reload. They can be added later as a layer on top: a behaviour component plus one system that dispatches to it.
- **Copy-in/copy-out component access through internal calls.** This was rejected because it is slow and every type needs its own glue. Instead, C# gets `Ref<T>` views straight into EnTT storage.

## Consequences

- EnTT 3.13 has no pool whose element type is only known at runtime, so the engine adds one byte-storage pool class, registered under a different name for each C# component type.
- `Ref<T>` points into EnTT storage, so a C# system's structural changes are deferred until the system returns, as with DOTS command buffers. A component added during a system is readable only from the next system on.
- C# components cannot hold managed references, such as strings or objects.
