Status: ready-for-agent

# Native functions any engine package can expose to C#, declared once per side, with string parameters in Iris

Origin: architecture review of Iris and FenrirScripting (2026-10-14), candidate 2, including the related Iris gap (internal calls accept only void, pointers and primitives). Follows the FenrirECS `ComponentType` work (`.scratch/fenrirecs-component-type`).

## Problem Statement

The engine is meant to be modular: "use only the modules they want". Scripting partly honours that. Another package or the app can already give C# its components and events through `ScriptingConfig`. But it cannot give C# a function. Two examples:
- FenrirPhysics wanting a raycast;
- FenrirInput wanting "is this key down".

Exposing either today means editing FenrirScripting. So scripting would end up knowing every package that wants to be scriptable, and an app could not leave a package out without scripting changing too.

Inside FenrirScripting, every native function costs four hand-matched edits, and nothing but a load-time check ties them together:
1. A C++ function over C types.
2. A `RegisterInternalCall` line with a hand-spelled name.
3. A field in the internal C# `Native` class, whose signature must match exactly.
4. A hand-written C# wrapper that turns strings into UTF-8 pointers and lengths.

There are other costs too:
- Each of the 21 core functions repeats the same "is a Scripting active?" guard and the same active-scene lookup.
- The package README describes this four-step process, and still refers to a helper (`Active()`) that the code calls `ActiveApp()`.

Underneath sits an Iris gap. A function registered as an internal call may take and return only void, sized numbers, `bool`, `char16_t` and pointers. So every string crosses as a `const char*` plus an `int32` length, which every C++ function validates itself (null pointer, negative length) and every C# wrapper encodes by hand.

## Solution

Any engine package (or the app) contributes to scripting through a **scripting extension** that it passes to `Scripting::Install`. An extension carries:
- its **native functions**;
- its components and events;
- the shared C# assembly that is its contract.

FenrirScripting knows nothing about the package beyond what the extension hands it. FenrirScripting's own engine API (Log, Time, Entity, components, events, queries, changes) becomes the built-in extension, using the same mechanism.

A native function is declared once on each side:
- **In C++:** an ordinary function whose first parameter is the **call context**. The context gives the App and the active scene's `EntityList`, and is resolved once, for every function, by scripting. The other parameters are the values C# passes, `std::string_view` among them. The function is named by the full name of the C# method that calls it.
- **In C#:** a `static partial` method marked `[NativeFunction]` in the extension's contract. A source generator writes the unmanaged function pointer field Iris binds, and the body that marshals the arguments (a `string` becomes UTF-8 bytes and a length).

If the two sides disagree (a missing function, a different signature, a name used twice), `Install` fails with a diagnostic naming the function, as any other scripting start-up failure does: logged, and the app runs without scripts.

Iris gains string parameters for internal calls, as an additive change under ADR-0006. A registered function may take `std::string_view`, which is UTF-8 and valid for the call. Managed code passes it as a byte pointer and a length, and the bridge binds that pair to the string parameter.

## User Stories

1. As an engine package author, I want to expose a C++ function to C# without editing FenrirScripting, so that my package stays optional and scripting stays ignorant of it.
2. As an engine package author, I want to describe everything my package gives scripting in one extension value, so that what my package contributes lives in my package.
3. As an engine package author, I want my extension to carry its native functions, components, events and C# contract assembly together, so that adding one package to scripting is one line for the app.
4. As an app author, I want to pass the extensions of the packages I use to `Scripting::Install`, so that C# sees exactly the packages my app uses.
5. As an app author, I want to leave a package's extension out and have scripting work without it, so that "use only the modules you want" holds for scripting too.
6. As an app author, I want my own app-level native functions, components and events to work the same way as a package's, so that there is one mechanism.
7. As an app author, I want existing `ScriptingConfig` fields for components, events and shared assemblies to keep working, so that my current setup does not break.
8. As an engine package author, I want to declare a native function in C++ as an ordinary function, so that I do not hand-write registration code.
9. As an engine package author, I want my C++ function to receive a call context giving the App and the active scene's entities, so that it can do its job without a global I reach for myself.
10. As an engine package author, I want my function called only while a Scripting is running, so that it never needs its own "is scripting active?" check.
11. As an engine package author, I want to say what C# gets back if my function is called when no Scripting is running, defaulting to a zero value, so that a stray call is harmless and predictable.
12. As an engine package author, I want to take a `std::string_view` parameter, so that I receive text without validating pointers and lengths myself.
13. As an engine package author, I want a C++ exception escaping my function to be stopped and logged, as Iris already does for internal calls, so that it never unwinds into C#.
14. As an engine package author, I want to name my C++ function by the full name of the C# method that calls it, so that one readable name ties the two sides together.
15. As a contract author, I want to declare a native function in C# as a `static partial` method with `[NativeFunction]`, so that the pointer field and the marshalling are generated, not hand-written.
16. As a contract author, I want `string` parameters marshalled to UTF-8 for me, with a lone surrogate becoming U+FFFD rather than failing, so that text crosses the same way everywhere.
17. As a contract author, I want sized numbers, `float`, `double`, `bool`, `char` and pointers to pass through unchanged, so that the declaration reads like the C++ one.
18. As a contract author, I want an `Entity` parameter or result to cross as its value, so that native functions can take and return entities naturally.
19. As a contract author, I want `in`, `ref` and `out` parameters of unmanaged structs to cross as pointers, so that structs such as a raycast hit can be passed without copying code.
20. As a contract author, I want a compile error, with a FENRIR diagnostic, when a `[NativeFunction]` method is not `static partial`, is generic, is overloaded, returns a string, or uses a type that cannot cross. Then a mistake is caught at build time, not at start-up.
21. As a contract author, I want my friendly C# API to call the generated method like any other, so that I add behaviour (such as turning error codes into exceptions) only where I want it.
22. As an app author, I want start-up to fail with a message naming the function if a C# declaration has no C++ function or a different signature, so that drift between the two sides is found before any script runs.
23. As an app author, I want start-up to fail with a message naming both extensions if two extensions declare the same native function name, so that a clash is never silently resolved.
24. As an app author, I want start-up failures from extensions to follow the same rule as every other scripting failure: logged, with the app running without scripts. Then nothing about extensions is fatal.
25. As a scripting maintainer, I want the engine's own 21 native functions to be the built-in extension, declared the same way, so that the core is the first user of the seam and the test extension the second.
26. As a scripting maintainer, I want the "is a Scripting active?" guard and the active-scene lookup written once, in the mechanism, so that no core function repeats them.
27. As a scripting maintainer, I want each core function to keep returning exactly what it returns today when scripting is not running (for example "not running" refusal codes and the null entity), so that C# behaviour is unchanged.
28. As a scripting maintainer, I want the hand-written `Native` class replaced by generated declarations, so that a core function is one C++ function and one C# declaration.
29. As a scripting maintainer, I want everything C# scripts see (Log, Time, Entity, components, events, queries) to behave exactly as before, so that the move is a pure refactor for script authors.
30. As a scripting maintainer, I want the README's "Adding an engine API" section to describe the new two-piece process, so that the documents stay true.
31. As an Iris user, I want to register a C++ function that takes `std::string_view`, so that text parameters need no pointer-and-length plumbing.
32. As an Iris user, I want managed code to pass such a string as a byte pointer and a length, which the bridge binds to the string parameter, so that Iris's binding check still catches a mismatched field.
33. As an Iris user, I want a null pointer with a non-zero length, or a negative length, to be refused at the boundary: my function is not called, the log sink gets an error, and managed code gets a zero result. Then bad input cannot reach my function.
34. As an Iris user, I want an empty string (null or not, with length zero) to arrive as an empty view, so that empty text is not an error.
35. As an Iris user, I want text with embedded NULs and multi-byte UTF-8 to arrive byte for byte, so that the length, not a terminator, defines the string.
36. As an Iris user, I want the change to be additive under ADR-0006, so that existing registrations and components are unaffected.
37. As a maintainer, I want the extension seam tested with a test-only extension driven from C#, so that the seam has two adapters (the built-in extension and the test one) and is verified as real.
38. As a maintainer, I want the reason for this design recorded in an ADR, so that a future review does not suggest folding package APIs back into scripting.

## Implementation Decisions

### Iris: string parameters for internal calls (additive, ADR-0006)

- `RegisterInternalCall<&Function>` accepts `std::string_view` parameters, alongside today's kinds. A string's bytes are UTF-8 and valid only for the call. They are not checked for valid UTF-8, because internal calls are a hot path; the scripting generator always sends valid UTF-8.
- **On the wire**, each string parameter is two parameters: a byte pointer and a 32-bit length.
  - The registration records the parameter's kind as `String`, reusing the existing value kind.
  - When the bridge binds a field, it matches a registered `String` against two consecutive field parameters: a pointer, then an `Int32`.
  - A field that declares anything else for it fails to bind, and the diagnostic names the field. The bridge function table does not change.
- **The thunk** Iris generates for the function turns the pair into a `std::string_view`:
  - A length of zero gives an empty view, whatever the pointer.
  - A negative length, or a null pointer with a non-zero length, means the function is not called. The thunk reports an Error through the log sink, the same path as a caught C++ exception, and returns a value-initialized result.
- **Not included:** string results, and structs by value. Strings are parameters only.
- The `RegisterInternalCall` documentation and the glossary's **Internal call** entry say that strings may be parameters.

### FenrirScripting: the scripting extension seam

- **A new public `ScriptingExtension`.** It holds:
  - a name, used in diagnostics;
  - the shared assemblies that are its C# contract;
  - its native functions;
  - its component registrations (`ComponentRegistration`);
  - its event registrations (`EventRegistration`).
- `ScriptingConfig` gains a list of extensions. Its existing `components`, `events` and `sharedAssemblies` stay, and behave as one more extension the app supplies.
- **A new public native-function description**, made from a C++ function pointer and a name:
  - The function's first parameter is the call context. The rest are the parameters C# passes, of any Iris internal-call kind, with `std::string_view` among them.
  - The name is the full name of the C# method that calls it (`Namespace.Type.Method`).
  - An optional value is returned to C# when no Scripting is running. The default is value-initialized.
  - A template in FenrirScripting builds the function Iris actually calls. That function finds the active Scripting's context once and calls the native function with it. If there is no active Scripting, it returns the "not running" value without calling the function.
- **The binding name.**
  - The Iris internal-call name is derived from the native function's name by one documented rule, which both FenrirScripting and the generator implement. One test pins the rule.
  - Neither side spells the generated field's name.
  - Overloads are not supported, so a name is unique per extension and across extensions.
- **The call context is a public FenrirScripting type.** It provides the App and the active scene's `EntityList`.
  - The rule, documented on the type: a native function may read and write component data but must not make structural changes (create or destroy entities, add or remove components), because the calling system's `Ref<T>`s point into storage.
  - Deferred structural changes for extension functions are out of scope.
  - The built-in extension's functions also need the component registry and the change buffer. They get those through an internal extension of the context that is not part of the public interface.
- **`Install` order.**
  1. Register every extension's native functions with Iris (process-wide and permanent, so registering the same function again on a later `Install` succeeds).
  2. Share the scripting contract plus every extension's assemblies.
  3. Register every extension's components and events, as `ScriptingConfig::components` and `events` are today.
- **`Install` failure, each in the existing stage-list failure path, logged, running without scripts:**
  - two extensions declaring one native function name, with a diagnostic naming both extensions;
  - Iris refusing a registration;
  - any shared assembly failing to bind, with Iris's diagnostic naming each field.
- **The built-in extension.**
  - The 21 core functions become native functions of the built-in extension.
  - Each keeps its current behaviour and its current "not running" value: the refusal code for "scripting is not running", the null entity value, zero or false.
  - The per-function guards and the repeated active-scene lookup are removed.
  - The single active-context slot (one Scripting per process) stays as it is, but only the mechanism reads it.

### C#: `[NativeFunction]` and its generator

- **The attribute.** The scripting contract gains `[NativeFunction]` for `static partial` methods in a partial class. A new incremental source generator, next to the existing ones, emits for each such method:
  - the public static unmanaged (Cdecl) function pointer field Iris binds, named by the binding-name rule;
  - the method body.
- **How each type crosses:**
  - sized integers, `float`, `double`, `bool`, `char` and pointers pass through;
  - `string` and `ReadOnlySpan<char>` parameters are encoded to UTF-8 (stack buffer for short text, as `Log` does now; a lone surrogate becomes U+FFFD) and passed as pointer and length;
  - `Entity` crosses as its `uint` value;
  - `in`, `ref` and `out` of an unmanaged struct cross as a pointer to it.
- **Diagnostics.** These are compile errors, numbered after the existing FENRIR ids:
  - not `static partial`;
  - a generic method or a generic containing type;
  - overloads of a native function;
  - a `string` result;
  - a parameter or result type that cannot cross;
  - a containing type that is not partial.
- **Where declarations live.** `[NativeFunction]` works in any assembly Iris binds: a contract, or a script assembly. It is meant for an extension's contract.
- **The core moves over.** The scripting contract's hand-written `Native` class is replaced by `[NativeFunction]` declarations. The friendly wrappers (`Log`, `Time`, `Entity`, `Events`, queries) call them and keep their own behaviour, such as turning refusal codes into exceptions with today's messages.

### Documents

- A new ADR (0009), "Engine packages extend scripting through scripting extensions". Scripting knows no package. A package contributes native functions, components, events and a contract through an extension. A native function is declared once per side and tied together by its C# method name.
- `GLOSSARY.md` gains:
  - **Scripting extension**: what one engine package, or the app, gives scripting. *Avoid:* plugin, module, since *module* is the review vocabulary and the Iris glossary avoids it for Iris components.
  - **Native function**: a C++ function C# calls, declared once per side; built on an Iris internal call. *Avoid:* binding, internal call (the Iris-level term), engine API.
  - **Call context**: what a native function receives: the App and the active scene's entities.
- **Internal call** gains string parameters.
- The FenrirScripting README's "Adding an engine API" section becomes "Exposing a native function": the C++ function, plus the C# declaration, plus listing them in an extension. The stale `Active()` reference goes.
- The Iris README's internal-call section is updated.

## Testing Decisions

- **Three seams, all existing public interfaces:**
  1. Iris's public interface: `RegisterInternalCall` plus a loaded Iris component calling the function.
  2. FenrirScripting's public interface: `ScriptingConfig` with extensions, plus `Scripting::Install`, driven from C#.
  3. The generator's diagnostics, through compile-fail CTests.

  No new internal seams.
- **A good test here** does what a caller does and asserts what a caller can observe:
  - values that arrive in C++ and come back to C#;
  - `Install` succeeding, or failing with a logged diagnostic that names the function or field;
  - log-sink errors;
  - compile errors with their FENRIR ids.

  It does not inspect generated field names, Iris's registration table, or the active-context slot. Expected values are literals.
- **Iris tests** extend `components.cpp` and the managed `InternalCallChecks`, with prior art in their existing internal-call cases:
  - a string parameter arrives byte for byte, for ASCII, multi-byte UTF-8, embedded NUL and empty text;
  - a function mixing string and numeric parameters gets each in order;
  - a field declaring a bare pointer, or the wrong order, for a string parameter fails to bind and the diagnostic names the field;
  - a null pointer with a non-zero length, or a negative length, is refused: the function is not called, an Error reaches the log sink, and the result is zero;
  - registering the same string-taking function twice succeeds.
- **Scripting tests** extend `ScriptingTests` with a test-only extension:
  - a small shared C# assembly with `[NativeFunction]` declarations, built like the existing fixture assemblies;
  - C++ native functions in the test executable;
  - the fixture script calling them.

  Prior art is `HeadlessApp`, `ConfigFor`, the `Scripted<>` helpers, and the "a missing contract is logged and the app keeps running" family. Cases:
  - a native function receives C#'s arguments, including a string and an `Entity`, and its result reaches C#;
  - through the call context it reads a component of an entity in the active scene, and writes component data C# then sees;
  - an extension's components and events are usable from C#;
  - leaving an extension out means its contract is not shared, and the rest of scripting works;
  - a C# declaration with no C++ function, or a different signature, fails `Install`: logged, naming the function, running without scripts;
  - two extensions with one function name fail `Install`, naming both;
  - a native function called after `Shutdown` returns its declared "not running" value;
  - the binding-name rule maps a sample of names exactly as documented.
- **The built-in extension** is covered by the whole existing scripting suite, which must pass unchanged. It already pins Log, Time, Entity, components, events, queries, changes and their refusal codes.
- **Generator tests** add compile-fail cases to the existing `CompileFail` project and `RefCompile-*` CTest loop, one per diagnostic: not static, not partial, non-partial containing type, generic, overloaded, string result, unsupported type. A control case that compiles is included, as the existing `Control` cases are.
- **The DemoApp** must build and run its scripting demo unchanged.

## Out of Scope

- **Structural changes from extension functions.** Deferred create, destroy, add and remove through the call context would be a later, additive feature.
- **More marshalling:**
  - string results, from Iris and from native functions;
  - structs by value;
  - arrays and spans other than `ReadOnlySpan<char>`;
  - callbacks from C++ into C#.
- **Real FenrirPhysics or FenrirInput extensions.** Those packages have no code yet; the test extension is the second adapter.
- **Native functions running on threads other than the one that runs the App's sequential systems** (see engine-scripting ticket 12, parallel C# systems).
- **Checking at build time that a C++ native function has a C# declaration.** A C++ function nobody declares is harmless and unused. Drift in the other direction fails `Install`.
- **Events and App's named queues** (review candidate 3), and **C# systems in the scheduler** (candidate 4).
- **Any change to the Iris bridge function table** or to existing Iris behaviour.

## Further Notes

- **Order of work:**
  1. The Iris string parameter, which is independent and can start at once.
  2. The extension seam, with the test extension and hand-written C# fields.
  3. The generator, with the test extension moving onto it.
  4. The core moving onto the seam and the generator.
  5. Documents and the ADR alongside.
- **Behaviour.** Everything C# scripts see must stay exactly as it is, including messages and refusal codes, and the existing scripting suite is the net for that. The refactor rule in CODING_STANDARDS applies to the core's move.
- **Install order is a constraint.** Registering an extension's functions with Iris must happen before the host shares its assembly, because binding happens when the assembly is shared. This is the same constraint the core bindings have today.
