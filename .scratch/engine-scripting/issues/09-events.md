# 09: Events across C++ and C#

**What to build:** `App` gains type-erased event queues keyed by name, with the existing double buffering (a read sees the current and previous frame, and the buffers rotate in `UpdateEvents`). `RegisterEvent<T>("Ns.Name")` makes a C++ event type's typed queue and the name-keyed queue the same queue, with its mapping validated like components. C#-only `IEvent` structs are discovered on load and get a byte queue with no C++. In C#, `ctx.Events.Send(e)` is deferred to the end of the system, and `ctx.Events.Read<T>()` yields a read-only view.

**Blocked by:** 08

**Status:** resolved

- [x] An event sent from C++ is read in C#, and one sent from C# is read in C++, with the same frame window semantics as native.
- [x] A C#-only event works with no C++ code.
- [x] A system doesn't see its own sends until it returns, and a read view stays valid for the whole system.
- [x] Existing C++ `SendEvent`/`ReadEvents` users are unchanged.

## Comments

### Resolution

- **App** (`FenrirApp`): `IEventQueue` gained type-erased `EventSize/EventAlignment/SendRaw/ReadRaw`; `App` gained `NameEvent<T>`, `FindEventQueue`, `CreateEventQueue` (a byte queue with the same two buffers, rotated in `UpdateEvents`) and `RemoveEventQueue`. A typed queue and its name are the same queue; an event that is not trivially copyable still works as before and reports a size of zero, so it cannot be used by name. `SendEvent`/`ReadEvents` and `EventQueue<T>`'s buffers are untouched.
- **Scripting**: `EventRegistration` and `MakeEvent<T, Mapping>` (public), `Scripting::RegisterEvent`, `ScriptingConfig::events`. Events live in a second table of the same registry (same generation). Four internal calls: `EventIdOf`, `RegisterScriptEvent`, `EventSend` (recorded in the change buffer, so applied when the system returns) and `EventRead`.
- **C#**: `IEvent`, `[NativeEvent]`, `SystemContext.Events` (`Send`, `Read`), `EventReader<T>`. The `EventGenerator` writes `EventTable` for C#-only events; FENRIR0014 refuses an event that is generic, managed or hidden.
- Load is all or nothing for events too: the names and layouts are checked before any queue is made, a failed load leaves no queue (tested), and `Shutdown` removes the queues the assembly made so the same App can load again (tested).
- Tests: C++ to C#, C# to C++, C#-only with no C++, the same frame window as a native reader (compared frame by frame), a system not reading its own sends, a view that survives 100 more sends, lookup errors, a mismatched layout, and the App-level API on its own.
- Read-only is proven by a compile-fail case (CS8332). A C#-sent event is checked across frame rotations from C++ by name, and a reloaded assembly sends and reads again.
- Not covered: a script-defined event whose name collides with a native one (the check exists in `CheckDiscoveredEvents`, with no test), and sending an event from a non-main thread (there is no such path yet).

Spec: `.scratch/engine-scripting/spec.md` (Model; Binding native code). The user requirement is that registering a C# event must need no C++.
