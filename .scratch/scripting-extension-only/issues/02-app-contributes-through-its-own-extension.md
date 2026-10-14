# 02: The app contributes through its own extension: remove the config's lists

**What to build:** `ScriptingConfig` loses `components`, `events` and `sharedAssemblies`. Its remaining fields only say where scripting finds things and how reloading behaves. Everything scripting knows comes from `ScriptingConfig::extensions`, and an app that shares its own components, events or contract does so through an extension of its own, as any package does (ADR-0009).

- The DemoApp's scripting helper builds and returns one extension, named for the app, holding the demo's component and its two events. The app adds it to the extensions.
- Install no longer has a "the config" source; ticket 01 already folds every source through one loop.

See the spec, `.scratch/scripting-extension-only/spec.md`, "Public interface of FenrirScripting", "DemoApp" and "Documentation".

**Blocked by:** 01 (Built-in extension carries the engine components and the scripting contract).

**Status:** resolved

- [x] `ScriptingConfig` has no `components`, `events` or `sharedAssemblies`. The doc comment on `extensions` says the app contributes through an extension of its own.
- [x] Install folds only extensions. Nothing refers to "the config" as a source.
- [x] The doc comments on `ComponentRegistration`, `EventRegistration`, `MakeComponent`, `MakeReadOnlyComponent` and `MakeEvent` say a registration goes into a `ScriptingExtension`. They no longer mention the config lists.
- [x] The DemoApp builds its scripting contribution as an extension and runs as before: the bobbing component and both events work from C#.
- [x] The tests that pushed onto the config's component or event lists put the same registrations in a test extension instead, with the same assertions. These include the mismatched-component test and the matching and mismatched C++ event tests.
- [x] The scripting README drops every "or `ScriptingConfig::components`/`events`" aside. Its "Scripting extensions" section shows an app's own extension as the way to share the app's components, events and contract.
- [x] ADR-0009 gains a consequence line: `ScriptingConfig` has no component, event or shared-assembly lists of its own, and the app gives an extension. The decision itself is unchanged.
- [x] Every scripting test passes, and the DemoApp builds.
