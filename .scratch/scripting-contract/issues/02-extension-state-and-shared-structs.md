# 02: Extension state and shared structs in FenrirScripting

**What to build:** the seam changes of ADR-0009 as amended on 2026-10-22 (spec: "The extension seam, without the back door"): an extension carries its state and its natives receive it; the built-in extension uses the same mechanism; shared structs are validated at script load.

- **`ScriptingExtension` state.** A typed setter (`template <class State> void SetState(std::shared_ptr<State>)`, name open) stores the value as `std::shared_ptr<void>` with its `std::type_index`. One state per extension; setting it twice replaces it.
- **Natives take it.** `NativeFunction::Make<&F>(name)` accepts `R F(const CallContext&, Args...)` (as today) and `R F(const CallContext&, State&, Args...)`, where `State` is any class type; `Detail::NativeFunctionTraits` records the expected `std::type_index` (none for the first form). `Install` fails with a diagnostic naming the function and both types when a native expects a state its extension does not carry, or any state when the extension carries none. The per-function static slot `Install` fills (beside `notRunning`) holds the `State*`, and the trampoline passes `*slot` after the context. `whenNotRunning` behaves as today for both forms.
- **The built-in extension on the seam.** `EngineContext` becomes the built-in extension's state, every native in `Bindings.cpp` takes `EngineContext&` as its second parameter, `CallContext` loses `m_engine` and the `Detail::CallContextAccess` friend, and `Detail::CallContextAccess::Engine` goes. `Detail::ActiveCallContext()` and the `activeContext` atomic stay as the single "a Scripting is running" switch.
- **Shared structs.** `ScriptingExtension::sharedStructs` (`std::vector<SharedStructRegistration>`, made by `MakeSharedStruct<T>("Namespace.Type")`): plain data natives take or return by pointer. `ScriptSchema.cpp` validates each against its mapping when a script assembly loads, as it does components and events, with the same all-or-nothing rule and the same diagnostics. Names are unique across every extension's components, events and shared structs together.
- **Docs.** `ScriptingExtension.hpp`'s and `README.md`'s extension section describe state, the two native forms and shared structs; the glossary already has *extension state* and *shared struct*.

**Blocked by:** none (independent of 01; stacked after it because 06 and 09 need both).

**Touches:** `packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp`, `packages/FenrirScripting/include/FenrirScripting/ComponentRegistry.hpp`, `packages/FenrirScripting/src/Bindings.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/src/ScriptSchema.cpp`, `packages/FenrirScripting/src/EngineContext.hpp`, `packages/FenrirScripting/Tests/`, `packages/FenrirScripting/README.md`.

**Status:** ready-for-agent

- [ ] FenrirScripting tests: an extension with state whose native takes `State&` receives the same object `SetState` was given; a native expecting a state the extension lacks, or the wrong type, fails `Install` with a diagnostic naming the function; a stateless native on a stateful extension still works.
- [ ] `grep -rn CallContextAccess packages/FenrirScripting` finds nothing; `CallContext` is `App*` only; the engine's natives pass through `EngineContext&`.
- [ ] A shared struct whose C# mirror drifts (a renamed field in the test fixture) fails the script load with a diagnostic naming the struct and field, and the app runs without scripts, as a drifted component does today.
- [ ] FenrirPhysicsScripting still builds and its tests pass with no change beyond what the compiler forces (its natives stay stateless in this ticket).
