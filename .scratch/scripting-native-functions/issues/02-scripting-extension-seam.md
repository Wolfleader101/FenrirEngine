# 02: The scripting extension seam, with a test extension

**What to build:** an engine package, or the app, can give C# its own native functions, components, events and C# contract without editing FenrirScripting (spec: `../spec.md`, "FenrirScripting: the scripting extension seam").

It passes a **scripting extension** to `Scripting::Install` through `ScriptingConfig`. A **native function** is an ordinary C++ function:
- its first parameter is the **call context**, which gives the App and the active scene's `EntityList`;
- it is named by the full name of the C# method that calls it;
- it optionally declares the value C# gets when no Scripting is running.

FenrirScripting resolves the context once for every native function, and registers every extension's functions with Iris before sharing the extensions' assemblies. Drift and clashes fail `Install` through the existing failure path: logged, with the app running without scripts.

This ticket proves the seam with a test-only extension whose C# side is hand-written for now: unmanaged function pointer fields named by the binding-name rule. Ticket 03 replaces them with generated declarations. The core's own functions are untouched until ticket 04.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] A public `ScriptingExtension` holds a name, the shared assemblies that are its contract, its native functions, its components and its events. `ScriptingConfig` gains a list of them. Its existing `components`, `events` and `sharedAssemblies` keep working unchanged.
- [x] A public native-function description is made from a C++ function pointer, whose first parameter is the call context and whose other parameters are any Iris internal-call kinds, plus the C# method's full name. It has an optional "not running" value, which defaults to value-initialized.
- [x] The public call context gives the App and the active scene's `EntityList`. Its documentation states that a native function must not make structural changes during a call.
- [x] A native function is called only while a Scripting is running. Otherwise C# gets its "not running" value and the function is not called.
- [x] One documented binding-name rule maps a native function's name to the Iris internal-call name. A test pins it on sample names.
- [x] `Install` registers every extension's native functions, then shares the contract plus every extension's assemblies, then registers every extension's components and events.
- [x] `Install` fails, logged and running without scripts, when:
  - two extensions declare one native function name, with a diagnostic naming both;
  - a C# field has no native function, with a diagnostic naming it;
  - signatures differ, with a diagnostic naming the field.
- [x] A test-only extension, made of a small shared C# assembly built like the existing fixtures plus C++ functions in the test executable, is driven from the C# fixture, through `ScriptingConfig` and `Install` only. It covers:
  - arguments, including an `Entity` value, arriving and a result returning;
  - reading and writing an entity's component data through the call context, which C# then sees;
  - the extension's components being usable from C#, and its events being registered and checked against their C# struct (reading an event needs a system; see Comments);
  - leaving the extension out, so its contract is not shared and the rest of scripting works (see Comments);
  - each failure above;
  - a call after `Shutdown` returning the "not running" value (not testable through the public seams; see Comments).
- [x] ADR-0009, "Engine packages extend scripting through scripting extensions", is recorded. `GLOSSARY.md` gains **Scripting extension**, **Native function** and **Call context**.
- [x] The full CTest suite passes, including the unchanged existing scripting suite and `ScriptingDoesNotUseEnTT`.

## Comments

Done. Three notes:

- The "not running" value has no behavioural test. Through the public seams, no C# code can run while no Scripting is active, so C# can never call a native function then. The value is guaranteed by construction: the function Iris calls checks the active call context before calling the native function.
- An extension's event is checked by its queue existing and the C# struct passing validation at load. C# can only read and send events inside a system (through `SystemContext`), and the extension script has no systems.
- "Leaving the extension out" has no new test of its own. The existing suite installs no extensions and passes unchanged, and Iris sharing is permanent for the process, so an "out" install after an "in" install in one test process would not show an unshared contract anyway.

The scripting contract is now shared first, before the app's `sharedAssemblies` and the extensions' contracts. Each assembly is bound as it is shared, and the others may refer to the contract.

Review follow-ups, done in this ticket:
- The "not running" value is set when `Install` registers the native function, not when it is made. So `Make` has no hidden effect, and `Make(name)` without a value really does give zero.
- `noexcept` C++ functions are accepted.
- A scripting test now passes a string (multi-byte and empty) through a native function.
- The config's own components and events, and every extension's, are registered by one loop. A failure caused by an event queue name already used by another type now says so, instead of "empty or duplicate name".

Left for ticket 04: `EngineContext` holds both `app` and a `CallContext` over the same App. They become one when the core's functions take the call context.
