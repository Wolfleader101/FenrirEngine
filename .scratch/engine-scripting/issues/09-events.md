# 09: Events across C++ and C#

**What to build:** `App` gains type-erased event queues keyed by name, with the existing double buffering (a read sees the current and previous frame, and the buffers rotate in `UpdateEvents`). `RegisterEvent<T>("Ns.Name")` makes a C++ event type's typed queue and the name-keyed queue the same queue, with its mapping validated like components. C#-only `IEvent` structs are discovered on load and get a byte queue with no C++. In C#, `ctx.Events.Send(e)` is deferred to the end of the system, and `ctx.Events.Read<T>()` yields a read-only view.

**Blocked by:** 08

**Status:** ready-for-agent

- [ ] An event sent from C++ is read in C#, and one sent from C# is read in C++, with the same frame window semantics as native.
- [ ] A C#-only event works with no C++ code.
- [ ] A system doesn't see its own sends until it returns, and a read view stays valid for the whole system.
- [ ] Existing C++ `SendEvent`/`ReadEvents` users are unchanged.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Model; Binding native code). The user requirement is that registering a C# event must need no C++.
