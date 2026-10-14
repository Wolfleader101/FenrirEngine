# 04: Move the core onto the seam and the generator

**What to build:** FenrirScripting's own engine API becomes the built-in scripting extension, declared exactly as any package's would be (spec: `../spec.md`, "The built-in extension" and "The core moves over"). The API is Log, Time, Entity, components, events, queries and changes: the 21 functions behind the scripting contract's internal `Native` class.

- **Native functions:** each core function becomes a native function taking the call context. The functions that need the component registry or the change buffer reach them through an internal extension of the context that is not public.
- **Guards:** the 21 copies of the "is a Scripting active?" guard and the repeated active-scene lookup disappear. Each function declares the "not running" value it returns today: the refusal code for "scripting is not running", the null entity, zero or false.
- **C# side:** the hand-written `Native` class becomes `[NativeFunction]` declarations. The friendly wrappers (`Log`, `Time`, `Entity`, `Events`, queries) call them and keep all their own behaviour and messages.

This is a pure refactor for script authors: nothing C# sees changes.

**Blocked by:** 03 (`[NativeFunction]` and its generator).

**Status:** resolved

- [x] The 21 core functions are native functions of a built-in extension that `Install` always includes. None of them checks for an active Scripting or looks up the active scene itself.
- [x] Each core function returns, when no Scripting is running, exactly what it returns today.
- [x] The component registry and change buffer reach core functions through an internal context that is not part of the public interface.
- [x] The scripting contract has no hand-written unmanaged function pointer fields. Its core functions are `[NativeFunction]` declarations, and the friendly wrappers call them.
- [x] Behaviour, messages, refusal codes and ordering seen from C# are unchanged: the existing scripting suite passes with no test edited for this ticket. `ScriptingDoesNotUseEnTT` passes.
- [x] The DemoApp builds and runs its scripting demo unchanged.
- [x] The FenrirScripting README and the doc comments no longer describe the `Native` class, `RegisterEngineBindings` or the stale `Active()` helper.
- [x] The full CTest suite passes.

## Comments

Done. The built-in extension is put first in the config's extensions by `Install`, so its functions go through the same name check, registration and sharing as any package's. A `CallContext` carries a private pointer to the engine's internal context; only the built-in functions reach it, through an accessor that is not in the public header.

Each function declares the "not running" value the old guard returned: -1 for `ComponentIdOf`, `EventIdOf`, `EventRead`, `QueryCount` and `QueryFill`; the `NotActive` code for `EntityDestroy`, `ComponentAdd`, `ComponentRemove` and `EventSend`; `Entity::Null` for `EntityCreate`; zero or false for the rest. They are unreachable from C# (nothing runs while no Scripting is), so this preserves the old contract rather than anything a test can observe.

One test-support file changed: the `BadReports` fixture looked the engine's field up by reflection under its old name; it now looks up `__native_<name>`. No C++ test case was edited.

One message changed: the start-up failure "the engine's internal calls could not be registered" is gone, since the built-in extension's functions are registered by the same stage as every extension's. A failure now reads "the native function '...' of extension 'built-in' could not be registered". No test pins either text.

Left alone, as they predate the change or are judgement calls: the repeated `name/length/typeName/typeLength` guard in `ComponentIdOf` and `EventIdOf`, and the "not running" values living in `BuiltInExtension` apart from the functions they describe.
