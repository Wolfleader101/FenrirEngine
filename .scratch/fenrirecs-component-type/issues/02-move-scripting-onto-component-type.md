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

**Status:** resolved

- [x] `ComponentRegistration` has a component type and no operation slots, selector, size or alignment.
- [x] `MakeComponent<T, Mapping>`, `MakeReadOnlyComponent<T, Mapping>` and `Scripting::RegisterComponent<T>` keep their signatures and meaning.
- [x] `MakeReadOnlyComponent` sets only the flag.
- [x] The `LiveEntity` helper is gone.
- [x] `MakeScriptComponent` is gone. Script-defined registrations are built from `ComponentType::Dynamic` plus their flags where a generation is reconciled.
- [x] Read-only and required components are refused for add and remove by checking the flags, with the same messages and skip behaviour as before. No refusal depends on an absent function slot.
- [x] Queries resolve each component id to its component type and query the span of component types. The empty-pool early-out uses count.
- [x] Change playback adds, gets and removes through the component type, and copies an added value using its size.
- [x] Dropping a script component whose layout changed clears its component type in every scene.
- [x] No scripting code calls `ComponentSelector`, `CountOf` or any `*Dynamic` method.
- [x] Scripting tests:
  - the cases that called registration slots directly are rewritten to use the component type with `EntityList`, asserting the same things;
  - three ECS-only cases are deleted: "a runtime view over native and script pools iterates the right entities", "script component storage honours alignment and never moves elements" and "the ECS holds dynamic components and refuses to change their shape while in use";
  - no new scripting tests are added.
- [x] The scripting test suite passes, and `ScriptingDoesNotUseEnTT` passes.
- [x] The DemoApp builds and runs its scripting demo unchanged.
- [x] The FenrirScripting README no longer describes registration operation slots.

## Comments

Done. One deviation: "script component storage honours alignment and never moves elements" was not only ECS behaviour. It also drove C# writing and reading a `Vector128` in an aligned component and using an empty component as a tag, which no other test covers. Its ECS part is deleted as planned (FenrirECSTests covers it); its C# part is kept as "C# uses an aligned component, and an empty one as a tag". The scripting suite has 69 cases (71 - 3 + 1).

`ComponentRegistration::type` has no default, so a registration can only be built with one (the factories, or designated initializers). The registry no longer checks for operation slots.
