# 04: Native component registry

**What to build:** The runtime registry that maps a component name to EnTT storage and its operations (get pointer, has, add, remove), since EnTT has no runtime type map. `scripting.RegisterComponent<T>("Fenrir.Transform")` registers a C++ component with an Iris `StructMapping`. On every script-assembly load, each registered mapping is validated explicitly against its C# struct (Iris 21), and any mismatch fails the load. Register the engine's `Transform` and `Name` as `IComponent` structs in the contract, and expose `Relationship` (hierarchy) read-only.

**Blocked by:** 03, Iris 20, Iris 21

**Status:** resolved

- [x] A C# `Ref<Transform>` from the registry reads and writes the same memory C++ sees through `GetComponent<Transform>`.
- [x] A deliberately mismatched C# struct fails the load, with a diagnostic naming the field.
- [x] `Name` (fixed-size text) round-trips both ways.
- [x] Writes to `Relationship` are impossible from C#.
- [x] A user component registered from outside FenrirScripting (in the test target) works with no engine changes.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Binding native code).

### Resolution

- **Two Iris gaps found and fixed first (Iris ticket 25).** The engine components live in the shared contract, not in a script assembly, and Iris only looked up mapped structs in the component's own assembly. It now also searches shared assemblies. A new `IRIS_FIELD_AS` lets a mapped field have a different managed name than the C++ member, so C# has `Position` and not `pos`.
- **Registry:** `ComponentRegistration` (public) holds a name and the operations get, has, add, remove, plus a validator for the C# struct. `MakeComponent<T, Mapping>(name)` and `MakeReadOnlyComponent<T, Mapping>(name)` build one from a type. The internal `ComponentRegistry` assigns ids and a generation. `Scripting::RegisterComponent<T>(name)` or `ScriptingConfig::components` register more; Transform, Name and Relationship are built in.
- **Validation:** every registered mapping is validated with `Component::ValidateStruct` when the script assembly loads, and a later registration is validated at once. Either way the first mismatch fails all-or-nothing: the assembly is released, the app keeps running, and the error names the component and the field.
- **C# side:** `Transform`, `Name`, `Relationship` and `EntityLink`, `Vector3`, `Quaternion` in the contract; `[NativeComponent]`; `Entity.Get<T>()`, `GetReadOnly<T>()` and `Has<T>()` (live, with clear exceptions). The registry id is cached per registry generation.
- **Read-only:** `IReadOnlyComponent` and `ReadOnlyRef<T>`. `IComponent` extends `IReadOnlyComponent`. `Relationship` implements only the read-only one, so there is no `Ref<Relationship>`, and assignments through the read-only view are compile errors. Five compile tests cover it. The generator now emits `ref readonly` accessors on `ReadOnlyRef<T>` for every component and runs on the contract assembly too.
- **Two real bugs this ticket's checks caught:**
  - `glm::quat` keeps `w` first in memory. The load validation failed on the first run and named the field. The C# `Quaternion` now declares `W, X, Y, Z`.
  - `Fenrir::Entity(id, list)` is a creating constructor (it replaces Transform, Name and Relationship). My first registry operations used it and silently reset the entity on every lookup. The operations now use `EntityList::GetEntity`, and the Transform and Name tests would fail again if that came back.
- **No engine change.** `Fenrir::Name` keeps its buffer private; like `Relationship`, it is described by a plain stand-in (`NameView`) with the same size, checked at compile time.
- **Two holes the reviews found, fixed:**
  - A script could declare its own `[NativeComponent("Fenrir.Relationship")]` struct implementing `IComponent` and get a writable view of the hierarchy, because only the contract's own type was layout-checked. A registration now remembers the C# type it was validated against, and the id lookup refuses any other type (`ComponentIdOf` takes the type's full name). A test proves the claim is refused.
  - C# remembered "not registered" per registry generation, but `RegisterComponent` did not change the generation, so a component registered after a failed lookup stayed invisible. Every registry change now gets a new generation, and a test looks the component up before and after registering it.
- **Load validation and reload.** Validation runs in the one place a script assembly loads, so it holds for every load by construction. Hot reload (ticket 10) must call it on the *new* generation before releasing the old one, so that "on any failure, the previous generation keeps running" holds; today a failure releases the only assembly there is.
- **Tests (seven new cases in the native test executable, five new compile tests):** a Transform is the same memory in C++ and C# both ways; a Name round-trips both ways, with multi-byte text and a name cut on a character boundary; the hierarchy is readable (parent, first child, sibling, null links) and unwritable; a custom `Health` component registered by the test target works with no engine changes; lookup failures explain themselves; a deliberately mismatched component fails the load and names the field, at load and when registered later.
