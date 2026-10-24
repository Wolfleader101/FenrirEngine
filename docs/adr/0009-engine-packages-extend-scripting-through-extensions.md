---
status: accepted
---

# Engine packages extend scripting through scripting extensions

The engine is modular: an app uses only the packages it wants. Scripting has to honour that. FenrirScripting knows no other engine package. A package that wants to be scriptable (a physics raycast, an input query) gives scripting a **scripting extension**, and the app passes the extensions of the packages it uses to `Scripting::Install`. An extension carries everything the package contributes:

- its **native functions**: C++ functions C# calls;
- its native components and C++ events, as `ComponentRegistration`s and `EventRegistration`s;
- its native resources, as `ResourceRegistration`s (*added by ADR-0029*);
- the shared assemblies that are its C# contract.

A native function is declared once on each side and tied together by one name, the full name of the C# method that calls it (`Namespace.Type.Method`):

- In C++ it is an ordinary function whose first parameter is the **call context**, which gives the App, the active scene's entities and (*since ADR-0029*) the change buffer. Scripting resolves the context once for every native function, so no function checks for a running Scripting itself.
- In C#, the contract holds the field Iris binds. The field's name follows from the C# method's name by one rule (`Namespace.Type.__native_Method`), so neither side spells the other's names.

`Install` registers every extension's native functions with Iris before it shares their contracts. If the two sides disagree (no C++ function for a field, different signatures, one name declared by two extensions), `Install` fails with a diagnostic naming the function, as any scripting start-up failure does: logged, with the app running without scripts.

We chose this over two alternatives:

- **Scripting binds every package's API itself.** Every scriptable package would have to be edited into FenrirScripting, which would then depend on all of them. An app could not leave a package out without scripting changing too.
- **Packages register Iris internal calls directly** (they always could, before `Install`). That leaves each package to reach the running App through a global of its own, to check for a running Scripting in every function, and to add its contract to the config's list of shared assemblies (since removed) separately from its functions. The pieces of one package's contribution would be spread across the app's setup.

## Consequences

- A package's scripting support lives in the package: its C++ native functions, its extension value, and its C# contract project.
- A native function may read and write component and resource data through the call context, but never makes a structural change directly: the calling C# system holds `Ref<T>`s into the ECS storage. *Amended by ADR-0029:* it records structural changes (creating and destroying entities, adding and removing components, inserting and removing resources) in the call context's change buffer, the one the engine's own entity functions use; a created entity is usable at once, and the rest plays back in order when the calling system returns. Extensions also carry native resources, which C# reaches through `Resources.Get<T>()`. *Amended by ADR-0039:* an extension may also declare stages, which C# systems join with `[StageSystem]` and which the extension runs inside its own C++ work.
- FenrirScripting's own engine API (Log, Time, Entity, components, events, queries, changes, resources) is the built-in extension, declared on the same mechanism: the methods of the contract's `Native` class are native functions named `Fenrir.Scripting.Native.<Method>`. It also carries the engine's components (Transform, Name, Relationship) and, as its shared assembly, the scripting contract (*since ADR-0029*, preceded by `Fenrir.Math`, the engine's math types, which the scripting contract is built on), and it is always the first extension. The core and any package's extension are two users of one seam.
- `ScriptingConfig` has no component, event or shared-assembly lists of its own: the app gives what it shares with C# as an extension, like any package.
- Nothing registers after `Install`: afterwards the registry changes only when a script reload removes and adds what the script assembly defines. A late registration, if one is ever brought back, must follow the same all-or-nothing rule as a reload: a mismatch releases only the script assembly and leaves nothing behind.
- Native function names are unique across extensions. Overloads are not supported.

*Amended 2026-10-22:* an extension carries its **extension state**: one value of a type it names, which a native function receives as its second parameter, after the call context, when it declares one (`R F(const CallContext&, State&, Args...)`; a stateless native keeps `R F(const CallContext&, Args...)`). `Install` fails with a diagnostic when a native names a state type its extension does not carry. This replaces reaching a package's state through a process-wide global or an App-level lookup, and it closes the built-in extension's private channel: the engine's own natives receive their state the same way, so the core and a package's extension are two users of one seam in fact, not only in name. An extension also lists its **shared structs**, the plain data its natives take or return by pointer, which are checked against their mappings when a script assembly loads, as components and events are. The declarations on both sides and the registration lists are generated (ADR-0016), so a native function and a shared type are each spelled once.
