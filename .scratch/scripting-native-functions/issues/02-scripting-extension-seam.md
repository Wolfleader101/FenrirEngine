# 02: The scripting extension seam, with a test extension

**What to build:** an engine package, or the app, can give C# its own native functions, components, events and C# contract without editing FenrirScripting (spec: `../spec.md`, "FenrirScripting: the scripting extension seam").

It passes a **scripting extension** to `Scripting::Install` through `ScriptingConfig`. A **native function** is an ordinary C++ function:
- its first parameter is the **call context**, which gives the App and the active scene's `EntityList`;
- it is named by the full name of the C# method that calls it;
- it optionally declares the value C# gets when no Scripting is running.

FenrirScripting resolves the context once for every native function, and registers every extension's functions with Iris before sharing the extensions' assemblies. Drift and clashes fail `Install` through the existing failure path: logged, with the app running without scripts.

This ticket proves the seam with a test-only extension whose C# side is hand-written for now: unmanaged function pointer fields named by the binding-name rule. Ticket 03 replaces them with generated declarations. The core's own functions are untouched until ticket 04.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] A public `ScriptingExtension` holds a name, the shared assemblies that are its contract, its native functions, its components and its events. `ScriptingConfig` gains a list of them. Its existing `components`, `events` and `sharedAssemblies` keep working unchanged.
- [ ] A public native-function description is made from a C++ function pointer, whose first parameter is the call context and whose other parameters are any Iris internal-call kinds, plus the C# method's full name. It has an optional "not running" value, which defaults to value-initialized.
- [ ] The public call context gives the App and the active scene's `EntityList`. Its documentation states that a native function must not make structural changes during a call.
- [ ] A native function is called only while a Scripting is running. Otherwise C# gets its "not running" value and the function is not called.
- [ ] One documented binding-name rule maps a native function's name to the Iris internal-call name. A test pins it on sample names.
- [ ] `Install` registers every extension's native functions, then shares the contract plus every extension's assemblies, then registers every extension's components and events.
- [ ] `Install` fails, logged and running without scripts, when:
  - two extensions declare one native function name, with a diagnostic naming both;
  - a C# field has no native function, with a diagnostic naming it;
  - signatures differ, with a diagnostic naming the field.
- [ ] A test-only extension, made of a small shared C# assembly built like the existing fixtures plus C++ functions in the test executable, is driven from the C# fixture, through `ScriptingConfig` and `Install` only. It covers:
  - arguments, including an `Entity` value, arriving and a result returning;
  - reading and writing an entity's component data through the call context, which C# then sees;
  - the extension's components and events being usable from C#;
  - leaving the extension out, so its contract is not shared and the rest of scripting works;
  - each failure above;
  - a call after `Shutdown` returning the "not running" value.
- [ ] ADR-0009, "Engine packages extend scripting through scripting extensions", is recorded. `GLOSSARY.md` gains **Scripting extension**, **Native function** and **Call context**.
- [ ] The full CTest suite passes, including the unchanged existing scripting suite and `ScriptingDoesNotUseEnTT`.
