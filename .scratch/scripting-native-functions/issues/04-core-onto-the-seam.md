# 04: Move the core onto the seam and the generator

**What to build:** FenrirScripting's own engine API becomes the built-in scripting extension, declared exactly as any package's would be (spec: `../spec.md`, "The built-in extension" and "The core moves over"). The API is Log, Time, Entity, components, events, queries and changes: the 21 functions behind the scripting contract's internal `Native` class.

- **Native functions:** each core function becomes a native function taking the call context. The functions that need the component registry or the change buffer reach them through an internal extension of the context that is not public.
- **Guards:** the 21 copies of the "is a Scripting active?" guard and the repeated active-scene lookup disappear. Each function declares the "not running" value it returns today: the refusal code for "scripting is not running", the null entity, zero or false.
- **C# side:** the hand-written `Native` class becomes `[NativeFunction]` declarations. The friendly wrappers (`Log`, `Time`, `Entity`, `Events`, queries) call them and keep all their own behaviour and messages.

This is a pure refactor for script authors: nothing C# sees changes.

**Blocked by:** 03 (`[NativeFunction]` and its generator).

**Status:** ready-for-agent

- [ ] The 21 core functions are native functions of a built-in extension that `Install` always includes. None of them checks for an active Scripting or looks up the active scene itself.
- [ ] Each core function returns, when no Scripting is running, exactly what it returns today.
- [ ] The component registry and change buffer reach core functions through an internal context that is not part of the public interface.
- [ ] The scripting contract has no hand-written unmanaged function pointer fields. Its core functions are `[NativeFunction]` declarations, and the friendly wrappers call them.
- [ ] Behaviour, messages, refusal codes and ordering seen from C# are unchanged: the existing scripting suite passes with no test edited for this ticket. `ScriptingDoesNotUseEnTT` passes.
- [ ] The DemoApp builds and runs its scripting demo unchanged.
- [ ] The FenrirScripting README and the doc comments no longer describe the `Native` class, `RegisterEngineBindings` or the stale `Active()` helper.
- [ ] The full CTest suite passes.
