# How does FenrirECS report component adds and removes?

Type: grilling
Status: resolved
Blocked by: none

## Question

Physics creates and destroys bodies when `RigidBody` and `Collider` components are added and removed ("How is physics modelled in the ECS?"), but FenrirECS exposes no notifications. Per ADR-0008, FenrirECS gains a neutral API rather than physics reaching for EnTT.

- Can it be built on EnTT's per-storage construct and destroy signals? Do they fire for FenrirECS's dynamic (C#) component storages, and for components added or removed from C#?
- Immediate callbacks, or a queue of what was added and removed that a system drains? A queue suits physics's batch before the step and keeps the order deterministic.
- Does destroying an entity report each component it removes?
- Its interface, with no EnTT type in it, and its cost when nothing listens.
- Should it also report edits (a dirty flag), given that C# writes through `Ref<T>` bypass any hook? Physics diffs edits itself, so physics does not need this.

## Answer

- **A queue a system drains, not immediate callbacks.** `ComponentWatch watch = entities.Watch(type);` (any `ComponentType`) is a move-only owner; while it exists it records every add and remove of that type as `{ Entity, Added | Removed }` in the order they happened, and `watch.Drain()` hands back the record and empties it. Built privately on EnTT's construct and destroy signals; no EnTT type in the interface (ADR-0008). Rejected: immediate callbacks, which would run inside whoever made the change (a C# system's deferred changes, an entity being destroyed), so physics would queue them anyway.
- **Facts, not interpretation:** add-then-remove within one drain, and a replace (`Entity::AddComponent` on an existing component removes then adds), appear as they happened. Each consumer decides what they mean; physics collapses each entity to its net effect (added-then-removed: nothing; removed-then-added: rebuild).
- **Both kinds of component type.** Native storages already signal (EnTT's default `sigh_mixin` storage, reached by typed calls, by the `ComponentType` path C# changes use, and by `registry.destroy`). `ByteStorage` (dynamic, C#-defined components) has no signals today and gains the same recording, so a watch never silently misses a C# component.
- **Destruction and clearing report removals:** destroying an entity reports `Removed` for each watched component it had; `EntityList::Clear` does too. Every add ends in a remove.
- **Several watches per type**, each independent (physics and a later package do not interfere). A watch belongs to one `EntityList` (one scene); a physics world, as a scene resource, holds its scene's watches, so lifetimes align (relies on the stable scene addresses from "What is the physics world's lifecycle?").
- **Cost:** nothing without a watch beyond EnTT's empty-signal check per operation.
- **Edits are not reported** (ADR-0011: physics diffs them).
