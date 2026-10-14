# 02: Move FenrirScripting onto `ComponentType`

**What to build:** the migrate step of the spec (`../spec.md`).

A scripting registration becomes only about what C# sees. `ComponentRegistration` holds one component type, in place of its get, has, add, remove, count and selector slots and its copied size and alignment. It keeps `name`, `typeName`, `readOnly`, `required`, `scriptDefined` and `validate`.

Native and script-defined registrations differ only in how their component type was made and in their flags. Everything that reached components through the slots now goes through the component type and `EntityList`:
- queries from C#;
- structural-change playback;
- the schema dropping a script component whose layout changed;
- the native functions behind `Entity.Get` and `Has`.

App authors' code does not change, and nothing C# sees changes: not behaviour, messages or ordering.

**Blocked by:** 01 (Add `ComponentType` and its `EntityList` operations beside the old ones).

**Status:** ready-for-agent

- [ ] `ComponentRegistration` has a component type and no operation slots, selector, size or alignment.
- [ ] `MakeComponent<T, Mapping>`, `MakeReadOnlyComponent<T, Mapping>` and `Scripting::RegisterComponent<T>` keep their signatures and meaning.
- [ ] `MakeReadOnlyComponent` sets only the flag.
- [ ] The `LiveEntity` helper is gone.
- [ ] `MakeScriptComponent` is gone. Script-defined registrations are built from `ComponentType::Dynamic` plus their flags where a generation is reconciled.
- [ ] Read-only and required components are refused for add and remove by checking the flags, with the same messages and skip behaviour as before. No refusal depends on an absent function slot.
- [ ] Queries resolve each component id to its component type and query the span of component types. The empty-pool early-out uses count.
- [ ] Change playback adds, gets and removes through the component type, and copies an added value using its size.
- [ ] Dropping a script component whose layout changed clears its component type in every scene.
- [ ] No scripting code calls `ComponentSelector`, `CountOf` or any `*Dynamic` method.
- [ ] Scripting tests:
  - the cases that called registration slots directly are rewritten to use the component type with `EntityList`, asserting the same things;
  - three ECS-only cases are deleted: "a runtime view over native and script pools iterates the right entities", "script component storage honours alignment and never moves elements" and "the ECS holds dynamic components and refuses to change their shape while in use";
  - no new scripting tests are added.
- [ ] The scripting test suite passes, and `ScriptingDoesNotUseEnTT` passes.
- [ ] The DemoApp builds and runs its scripting demo unchanged.
- [ ] The FenrirScripting README no longer describes registration operation slots.
