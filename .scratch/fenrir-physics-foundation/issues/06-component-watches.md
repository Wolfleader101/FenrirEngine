# 06: Component watches

**What to build:** A package can find out, in order and in a batch, which entities gained or lost a component type (spec: "FenrirECS"; glossary: component watch). `EntityList::Watch(ComponentType)` returns a move-only watch that records every add and remove of that type as `{ entity, Added | Removed }` until drained. Built privately on EnTT's construct and destroy signals, with no EnTT type in its interface (ADR-0008); dynamic (C#-defined) storages record the same way.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] `FenrirECSTests` pin, for a native and a dynamic component type: adds and removes appear in operation order; draining empties the record; a replace (adding a component the entity already has) records remove then add; destroying an entity and clearing the list record a removal per watched component.
- [ ] Several watches on one type record independently; a watch records nothing that happened before it was created; destroying a watch stops recording and costs nothing afterwards.
- [ ] Changes made through the `ComponentType` path (the one C# structural changes use) are recorded.
- [ ] Edits to component values are not recorded.
- [ ] No EnTT type appears in the watch's interface.
