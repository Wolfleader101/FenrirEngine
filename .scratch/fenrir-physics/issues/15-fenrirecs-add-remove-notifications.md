# How does FenrirECS report component adds and removes?

Type: grilling
Status: open
Blocked by: none

## Question

Physics creates and destroys bodies when `RigidBody` and `Collider` components are added and removed ("How is physics modelled in the ECS?"), but FenrirECS exposes no notifications. Per ADR-0008, FenrirECS gains a neutral API rather than physics reaching for EnTT.

- Can it be built on EnTT's per-storage construct and destroy signals? Do they fire for FenrirECS's dynamic (C#) component storages, and for components added or removed from C#?
- Immediate callbacks, or a queue of what was added and removed that a system drains? A queue suits physics's batch before the step and keeps the order deterministic.
- Does destroying an entity report each component it removes?
- Its interface, with no EnTT type in it, and its cost when nothing listens.
- Should it also report edits (a dirty flag), given that C# writes through `Ref<T>` bypass any hook? Physics diffs edits itself, so physics does not need this.
