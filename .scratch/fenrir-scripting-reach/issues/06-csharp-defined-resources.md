# 06: C#-defined resources

Spec: [C# reach: parenting, scenes, the app, C# resources and mesh positions](../spec.md).

**What to build:** A C# script assembly defines its own resources: a blittable struct marked `[SceneResource]` or `[AppResource]`, listed with its layout in the script report beside C# components and checked at admission (a struct that is not blittable is refused, the error naming it), its memory native and laid out from the report. An app resource is made default-initialised when its assembly is admitted; a scene resource is made in every existing scene at admission and in each new scene, and ends with its scene. On a reload, a resource with an unchanged layout keeps its value, one whose layout changed resets to its defaults with a warning naming it, and one whose type is gone is removed. `Resources.Get`, `TryGet`, `GetReadOnly`, `Insert` and `Remove` reach it as they reach native resources. ScriptingDemo's showcase gains a row: a C#-defined resource counting something shown on screen.

**Blocked by:** None (can start immediately)
**Touches:** `docs/adr/0029-*.md`, `GLOSSARY.md`, `packages/FenrirScripting/src/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, `examples/ScriptingDemo/`

**Status:** ready-for-agent

Stacked on 05 (top of the stack).

- [ ] Tests: a scene resource and an app resource admitted from the fixture and read and written through `Resources`.
- [ ] A test: a non-blittable resource struct is refused at admission with an error naming it.
- [ ] Tests: a scene resource exists in scenes that existed at admission and in a scene created later, and is gone when its scene is destroyed.
- [ ] Reload tests: an unchanged layout keeps the value; a changed one resets with a warning; a removed type is removed.
- [ ] ScriptingDemo shows the counter (a hand check), and its smoke run passes.
- [ ] The API description and generated bindings are current.
- [ ] ADR-0029's "resources defined in C# by script assemblies ... left for later" is amended, and GLOSSARY.md's Scene resource and App resource say a script assembly may keep one.
