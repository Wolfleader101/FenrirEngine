# 01: Iris and the SDK pass enums and one-field wrappers by value

**What to build:** A native function takes and returns an enum, and a struct of one field, by value, so a generated declaration reads as the C++ API does (spec: "The runtime boundary"; ADR-0044).

- **Iris, enums:** `Detail::InternalCallParameterType` and `InternalCallResultType` (`Iris/Native/include/Iris/InternalCall.hpp`) accept `std::is_enum_v<T>`; `WireOf<E>` wires `std::underlying_type_t<E>` and casts in `From`; `InternalKindOf<E>` is `KindOf<std::underlying_type_t<E>>`; the thunk's `Default()` and the result path cast back. The managed bridge (`Iris/Managed/Iris.Bridge/InternalCalls.cs`) is unchanged: the field's parameter is the integer kind.
- **Iris, wrappers:** a trait `Iris::WrapperOf<T>` (name and shape settled in the ticket; it names the field's type and converts both ways) that a host specialises for a one-field struct. `WireOf<T>` for a specialised type wires the field; both concepts accept it. A struct with no specialisation still fails to compile, with a `static_assert` naming it.
- **SDK, `NativeFunctionGenerator`** (`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`): `PassesThrough` accepts an enum (field type its underlying type, argument `(underlying)p`, result `(E)call`) and an unmanaged struct with exactly one instance field or one auto-property of a pass-through type (field type that type, argument `p.Member`, result `new T(…)` or an object initialiser; the ticket settles how a wrapper without a one-argument constructor is built). The `Entity` special case and `IsEntity` are deleted: `Entity` is a wrapper of `Value`. FENRIR0026's message names the new forms.
- **Docs:** Iris's README section on internal calls, `FenrirScripting/README.md`'s native-function section, and `NativeFunction`'s doc in `ScriptingExtension.hpp` list enums and wrappers among what crosses.

**Blocked by:** none

**Touches:** `packages/Iris/Native/`, `packages/Iris/Managed/Iris.Bridge/`, `packages/Iris/Tests/`, `packages/Iris/README.md`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`, `packages/FenrirScripting/Tests/CompileFail/`, `packages/FenrirScripting/Tests/`, `packages/FenrirScripting/README.md`, `packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp`

**Status:** ready-for-agent

- [ ] Iris tests: an enum parameter and result, and a wrapper parameter and result, cross an internal call over the fixture; the managed side declares the integer; a two-field struct by value fails to compile (a compile-time test or a `static_assert` check).
- [ ] SDK: the scripting harness calls a native taking an enum and a one-field struct and getting each back; compile-fail fixtures for a two-field struct and a wrapper whose field is a `string`; every existing `Entity`-taking native still binds with the special case gone.
- [ ] `FenrirScripting` and `Iris` test targets pass on `msvc-x64-debug`; the generated bindings are unchanged (nothing emits the new forms yet).
