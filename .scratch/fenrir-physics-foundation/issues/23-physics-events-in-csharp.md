# 23: Physics events in C#

**What to build:** C# scripts react to contacts and triggers (spec: "FenrirPhysics: scripting extension"; ADR-0012). `ContactBegin`, `ContactEnd`, `TriggerEnter` and `TriggerExit` are registered as native events with C# mirror structs, read through `EventReader<T>` fields, each event once.

**Blocked by:** 08 (Event readers in C#, and scripting migrates); 17 (Triggers and contact events); 22 (Physics in C#: components and native functions).

**Status:** resolved

- [ ] A fixture C# system reads one `ContactBegin` when a box with `ContactEvents` lands, with the same entities, point and normal C++ sees.
- [ ] A fixture C# system reads one `TriggerEnter` and one `TriggerExit` for a body passing through a trigger.
- [ ] C# toggling `Trigger.enabled` or `ContactEvents.enabled` takes effect on the next tick.
- [ ] The event mirrors validate at load like other native events.

**Resolved by:** merge of impl/23-physics-events-csharp (160b10f)
