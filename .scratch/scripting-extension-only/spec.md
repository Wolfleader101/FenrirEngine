Status: ready-for-agent

# Make the scripting extension the only way into scripting

Origin: architecture review of Iris and FenrirScripting (2026-10-14), candidate 1. Follows `.scratch/scripting-native-functions`, which introduced the scripting extension and moved the engine's native functions onto it.

## Problem Statement

ADR-0009 says that an engine package, or the app, gives scripting what it contributes through one **scripting extension**. The engine's own API is described as "the built-in extension, declared on the same mechanism": the core and any package are "two users of one seam".

The code doesn't match that today. There are four ways for something to get into scripting, and each one has its own rules:

1. **The extensions** (`ScriptingConfig::extensions`): native functions, native components, C++ events and shared assemblies.
2. **The config's own lists**: `ScriptingConfig::components`, `events` and `sharedAssemblies` copy three of an extension's four fields. Install folds them in as an extra source called "the config". The DemoApp uses this route rather than an extension.
3. **Late registration**: `Scripting::RegisterComponent`, `RegisterEvent` and their three templates, called after Install. Its rules differ from Install's:
   - only `RegisterEvent` checks that an App is attached;
   - the registration goes into the registry *before* it is checked, and stays there if the check fails;
   - a failed check calls the full scripting shutdown. That stops the .NET host and hot reload, and frees the engine context. The header promises only that "the script assembly" is released.
4. **The engine's own pieces**, which bypass the built-in extension:
   - Transform, Name and Relationship go into the registry through their own call before any extension is read;
   - the Fenrir.Scripting contract is shared through its own path, ahead of both the config's shared assemblies and every extension's.

   The built-in extension carries only native functions.

The costs:

- **For an engine integrator:** there are three ways to give scripting a component, and the header and README describe each one separately. The integrator has to learn which to use, and when the late way is safe.
- **For a maintainer:** the rules for "what scripting knows" live in three places. A late registration that doesn't match its C# struct quietly turns off hot reload for the rest of the run, and nothing tests that.
- **For a package author:** the core has routes that packages don't have, so the claim "your package uses the same seam as the engine" holds only partly.

## Solution

From the integrator's point of view, everything scripting knows comes from the extensions passed to `Scripting::Install`, and nothing else:

- A package passes its extension.
- The app passes its own extension, for its components, events and contract.
- The engine's built-in extension is the first one, and now carries all four kinds of contribution:
  - its native functions (as today);
  - the engine components Transform, Name and Relationship;
  - no C++ events (it has none);
  - the Fenrir.Scripting contract as its shared assembly.

`ScriptingConfig` keeps only the settings for where things are and how they behave: the script assembly, the bridge directory, the .NET root, the extensions, and the hot-reload settings. `Scripting` has no registration calls. Once Install has run, only the script assembly itself can change the registry, by reloading with a new set of script-defined components and events.

Nothing changes for script authors. The engine components keep their names, layouts and rules. Every startup failure is still logged and leaves the app running without scripts.

## User Stories

1. As an engine integrator, I want exactly one way to give scripting a native component, so that I don't have to choose between three.
2. As an engine integrator, I want exactly one way to share a C++ event with C#, so that the app and every package use the same mechanism.
3. As an engine integrator, I want exactly one way to share a C# contract assembly, so that a package's functions and its contract always travel together.
4. As an engine integrator, I want my app's own components, events and contract to go in an extension of their own, so that the app is just another contributor, as ADR-0009 says.
5. As an engine integrator, I want `ScriptingConfig` to describe only where scripting finds things and how reloading behaves, so that what scripting knows is set in one place: the list of extensions.
6. As an engine integrator, I want the extension's name to appear in every diagnostic about one of its components or events, so that I know which contributor to fix.
7. As an engine integrator, I want an empty or duplicate component name in any extension to fail Install with a diagnostic naming that extension, so that I learn about it at start-up.
8. As an engine integrator, I want an extension component named like an engine component (for example "Fenrir.Transform") to fail Install, naming that extension, so that no package can shadow the engine's components.
9. As an engine integrator, I want an extension event whose name is already taken by an app queue of another type to fail Install, naming that extension, as it does today.
10. As an engine integrator, I want a native component that doesn't match its C# struct to fail the load all-or-nothing, with the component and field named, wherever it came from.
11. As an engine integrator, I want `FindComponent` to keep finding the engine's components, every extension's components, and the script-defined ones, so that native code can still reach any component by name.
12. As an engine integrator, I want the engine components to keep their flags (Transform and Name required, Relationship required and read-only), so that scripts can still not remove them or write the hierarchy.
13. As an engine integrator, I want the Fenrir.Scripting contract still shared before any other extension's contract, so that package contracts that refer to it still bind.
14. As an engine integrator, I want a missing Fenrir.Scripting contract in the bridge directory to fail Install with the same diagnostic as today.
15. As an engine integrator, I want a project with no script assembly to start no .NET and register nothing, as today.
16. As an engine integrator, I want no way to change scripting's registry after Install other than a script reload, so that a mistake can't turn off hot reload partway through a run.
17. As an engine integrator, I want the scripting README to describe the extension as the only way in, with an example of an app's own extension, so that the documentation matches the interface.
18. As a package author, I want the engine's components to come in through the built-in extension, using the same fields my extension has, so that the core and my package really are two users of one seam.
19. As a package author, I want my extension's contract shared and its native functions registered exactly as the built-in extension's are, so that nothing the core does is closed to me.
20. As a package author, I want two extensions declaring one native function to keep failing Install with a diagnostic naming both, as today.
21. As a script author, I want my C# components, events and systems to behave exactly as before, so that this change makes no difference to me.
22. As a script author, I want the engine components to keep their C# names and layouts, so that my scripts still compile and load.
23. As a script author, I want a lookup that failed before a registry change to succeed afterwards. The registry now changes only on reload, so that is the case that has to keep working. *(Implementation note: met by construction. A reload brings new script types with empty id caches, so no test over a reload can tell the difference. See ticket 03's Comments.)*
24. As a maintainer, I want one loop that turns extensions into registrations, so that the rules for empty names, duplicates and event queues live in one place.
25. As a maintainer, I want the engine components' layout checks to stay with their mappings and run whenever a script assembly loads, as today.
26. As a maintainer, I want the bug where a failed late check shuts down the host to disappear along with late registration, rather than being fixed separately.
27. As a maintainer, I want ADR-0009 to record that the config has no lists of its own and that nothing registers after Install, so that a later review doesn't suggest adding a second way in again.
28. As a maintainer, I want the DemoApp to build its scripting contribution as an extension, so that the example teaches the only way.
29. As a user running a game, I want start-up and frame cost unchanged.

## Implementation Decisions

**Public interface of FenrirScripting.** This is a breaking change, but FenrirScripting isn't covered by ADR-0006's freeze, which applies only to Iris.

- `ScriptingConfig` loses `components`, `events` and `sharedAssemblies`. It keeps:
  - `scriptAssembly`, `bridgeDirectory`, `dotnetRoot`;
  - `extensions`;
  - `hotReload`, `reloadDirectory`, `reloadInterval`, `reloadDebounce`, `unloadTimeout`.

  Their meaning doesn't change, except that the `extensions` comment now says the app contributes through an extension of its own.
- `Scripting` loses:
  - `RegisterComponent(ComponentRegistration)`;
  - `RegisterEvent(EventRegistration)`;
  - the three templates built on them: `RegisterComponent<T, Mapping>`, `RegisterReadOnlyComponent<T, Mapping>` and `RegisterEvent<T, Mapping>`.

  `FindComponent`, `Reload`, `Generation`, `PendingUnloadCount`, `IsRunning`, `HasScripts` and `Shutdown` keep their meaning.
- `ComponentRegistration`, `EventRegistration`, `MakeComponent`, `MakeReadOnlyComponent` and `MakeEvent` keep their shape. Their doc comments now say a registration goes into a `ScriptingExtension`. The `required` flag keeps its meaning: scripts may not remove the component.
- `ScriptingExtension` and `NativeFunction` don't change.

**The built-in extension.**

- It is built once Install knows the bridge directory, because its shared assembly is the Fenrir.Scripting contract from that directory. Install still checks that the contract file exists, with today's diagnostic, before building the extension.
- It carries:
  - the built-in native functions, unchanged;
  - Transform, Name and Relationship, made with the same mappings and flags as today;
  - no events;
  - the contract as its only shared assembly.
- The function that today adds the engine components to the registry becomes one that returns their registrations for the built-in extension. The mappings and layout `static_assert`s stay with it.
- It is still inserted first among the extensions. That keeps two orderings that existing behaviour depends on:
  - the engine components are registered before any other, so another extension can't take their names;
  - the contract is shared before every other shared assembly.
- Its name is still "built-in", and diagnostics use that name.

**Install's assembly of extensions.**

- There is one loop over the extensions, the built-in one included, in order. For each extension it adds the components, then binds and adds the events. It refuses an empty or duplicate name, or an event queue of another type, and the diagnostic names the extension: "extension '<name>'".
- The "the config" source and the separate engine-component stage are removed.
- The host's shared assemblies are every extension's shared assemblies, in extension order. The separate contract-first insertion and the config's list are removed.
- The checks on native function names and the order of registration are unchanged.

**After Install.** The registry holds the extensions' registrations for the life of the `Scripting`. It changes only when a script reload removes and adds script-defined components and events, as today. The registry generation that C# watches still increases on each such change.

**DemoApp.** Its scripting helper builds and returns one extension, named for the app. That extension holds the demo's component, its two events, and the demo's shared assemblies if it has any. The app adds it to `ScriptingConfig::extensions`.

**Documentation.**

- The scripting README:
  - drops the "or `ScriptingConfig::components`/`events`" asides and the late-registration examples;
  - presents the app's own extension in the "Scripting extensions" section;
  - says that the engine components come from the built-in extension.
- ADR-0009 gains one consequence line: `ScriptingConfig` has no component, event or shared-assembly lists of its own, the app gives an extension, and nothing registers after Install. The ADR's decision is unchanged. This line only records how it is enforced.
- `GLOSSARY.md` needs no new term. *Scripting extension* already says "one engine package, or the app".

## Testing Decisions

- **Seam:** the existing one, `Scripting::Install` with `ScriptingConfig::extensions`, driven by the package's .NET-backed test executable with the fixture script assembly. Behaviour is observed through:
  - `HasScripts`, `IsRunning` and `FindComponent`;
  - the logger;
  - C# lookups made through the fixture's registry methods, as existing tests already do.

  No new seam is introduced. The tests that need no .NET don't change.
- **What a good test is here:** it states what an integrator sees, such as "Install failed, the log names extension 'X' and the component", or "FindComponent finds 'Fenrir.Transform', and it is required". It doesn't depend on the order of the registry's internals or on how the built-in extension is put together.
- **Tests to convert:**
  - Tests that push onto `config.components` or `config.events` (a mismatched component, a matching or mismatched C++ event) put the same registrations in a test extension instead. Their assertions don't change.
  - The test that a mismatched late registration is caught at once becomes a mismatch that fails Install, through an extension. It folds into the existing "a mismatched component fails the load and names the field" test if that test already covers it.
  - The test that a field of the wrong type is caught becomes an Install-time failure through an extension.
  - The duplicate-name and empty-name late-registration checks become Install failures naming the extension.
  - The moved-from `Scripting` test drops its two registration calls.
- **Test to move:** the test that "a lookup that failed before a registry change succeeds after it" is now expressed over a reload that adds a script-defined component, since a reload is the only remaining registry change. The existing reload tests and their rebuilt fixture assemblies are the prior art. *(Not done: such a test would be tautological. See ticket 03's Comments.)*
- **New tests:**
  - The engine components are found after Install, with their flags (Transform and Name required, Relationship required and read-only).
  - An extension that declares a component named "Fenrir.Transform" fails Install, and the log names that extension.
  - An extension whose shared contract refers to Fenrir.Scripting still loads. The existing test extension (`Tests/Extension` with `Tests/ExtensionScript`) already covers this; keep it passing.
- **Prior art:**
  - "two extensions declaring one native function fail Install";
  - "a mismatched component fails the load and names the field";
  - the test extension and its script;
  - the reload tests.

## Out of Scope

- Pulling the extension-assembly rules out of Install into a pure module testable without .NET (review candidate 2). It builds naturally on this spec's single loop, but it is its own spec.
- Giving the call context the capabilities packages would need: deferred structural changes, and resolving a component id. Also removing the built-in extension's private access to the registry and change buffer (review candidate 9). That waits for the first real package extension.
- Moving Transform, Name and Relationship out of FenrirScripting into an extension supplied by the package that owns them. FenrirECS can't depend on scripting, so this needs its own decision.
- Replacing the process-wide call context with internal calls that carry a context (review candidate 3, an additive Iris change).
- Keeping late registration with all-or-nothing rules. This was considered and rejected: it would leave a second way in, and only tests used it.
- Any change to Iris.

## Further Notes

- The late-registration shutdown bug isn't fixed separately: it goes away with the calls. If anyone asks to bring late registration back, ADR-0009's new consequence line is the answer. A reintroduced late registration would need the same all-or-nothing rule as a reload, releasing only the script assembly and leaving nothing behind.
- After this change the number of ways into scripting is one, so the sentence in ADR-0009 about the core and packages being "two users of one seam" becomes true for components, events and contracts. Native functions already used the seam. Only call-context access (candidate 9) remains asymmetric.
