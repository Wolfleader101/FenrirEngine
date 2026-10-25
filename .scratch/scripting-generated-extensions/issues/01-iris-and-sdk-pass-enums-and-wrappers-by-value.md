# 01: Iris and the SDK pass enums and one-field wrappers by value

**What to build:** A native function takes and returns an enum, and a struct of one field, by value, so a generated declaration reads as the C++ API does (spec: "The runtime boundary"; ADR-0044).

- **Iris, enums:** `Detail::InternalCallParameterType` and `InternalCallResultType` (`Iris/Native/include/Iris/InternalCall.hpp`) accept `std::is_enum_v<T>`; `WireOf<E>` wires `std::underlying_type_t<E>` and casts in `From`; `InternalKindOf<E>` is `KindOf<std::underlying_type_t<E>>`; the thunk's `Default()` and the result path cast back. The managed bridge (`Iris/Managed/Iris.Bridge/InternalCalls.cs`) is unchanged: the field's parameter is the integer kind.
- **Iris, wrappers:** a trait `Iris::WrapperOf<T>` (name and shape settled in the ticket; it names the field's type and converts both ways) that a host specialises for a one-field struct. `WireOf<T>` for a specialised type wires the field; both concepts accept it. A struct with no specialisation still fails to compile, with a `static_assert` naming it.
- **SDK, `NativeFunctionGenerator`** (`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`): `PassesThrough` accepts an enum (field type its underlying type, argument `(underlying)p`, result `(E)call`) and an unmanaged struct with exactly one instance field or one auto-property of a pass-through type (field type that type, argument `p.Member`, result `new T(…)` or an object initialiser; the ticket settles how a wrapper without a one-argument constructor is built). The `Entity` special case and `IsEntity` are deleted: `Entity` is a wrapper of `Value`. FENRIR0026's message names the new forms.
- **Docs:** Iris's README section on internal calls, `FenrirScripting/README.md`'s native-function section, and `NativeFunction`'s doc in `ScriptingExtension.hpp` list enums and wrappers among what crosses.

**Blocked by:** none

**Touches:** `packages/Iris/Native/`, `packages/Iris/Managed/Iris.Bridge/`, `packages/Iris/Tests/`, `packages/Iris/README.md`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`, `packages/FenrirScripting/Tests/CompileFail/`, `packages/FenrirScripting/Tests/`, `packages/FenrirScripting/README.md`, `packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp`

**Status:** resolved

- [x] Iris tests: an enum parameter and result, and a wrapper parameter and result, cross an internal call over the fixture; the managed side declares the integer; a two-field struct by value fails to compile (a compile-time test or a `static_assert` check).
- [x] SDK: the scripting harness calls a native taking an enum and a one-field struct and getting each back; compile-fail fixtures for a two-field struct and a wrapper whose field is a `string`; every existing `Entity`-taking native still binds with the special case gone.
- [x] `FenrirScripting` and `Iris` test targets pass on `msvc-x64-debug`; the generated bindings are unchanged (nothing emits the new forms yet).

## Comments

**The `WrapperOf` trait's shape (settled).** As the spec sketched: `Iris::WrapperOf<T>` is an empty primary template a
host specialises with `using Value = …;`, `static T From(Value)` and `static Value To(T)` (taking `T` or `const T&`).
`Detail::InternalCallWrapper<T>` accepts a class type whose specialisation has all three, with `From` returning exactly
`T` and `To` exactly `Value`, and `Value` one wire value: a `PrimitiveValue`, an enum with a primitive underlying type, or a
pointer. A wrapper of a wrapper, or of a `std::string_view`, is not one. A function, not a member pointer, so a struct
with a private field or a constructor of its own (an id class) can still be a wrapper; `From` and `To` run inside the
thunk's `try`, so an exception from either is stopped like one from the native. A wrapper result that the boundary
replaces (a refused string, an exception) is the zero wire value, not `To(T{})`, so `Default()` stays `noexcept`.
`RegisterInternalCallImpl` now checks with `static_assert`s instead of a `requires` clause: a struct without the
specialisation fails with "Iris passes a struct to or from a native function by value only as a wrapper", the compiler
naming the struct in the instantiation, and anything else unsupported with "Iris cannot pass this C++ type to or from a
native function".

**Building a wrapper result without a one-argument constructor (settled).** The generator builds a wrapper result with
`new T(call)` when `T` has an instance constructor of one by-value parameter of exactly the member's type that the
declaring type can call; otherwise with `new T { Member = call }` when the member is a non-readonly field, or a property
whose `set` or `init` accessor the declaring type can call; otherwise the result is refused with FENRIR0026 (the struct
can still be a parameter). The member is the struct's only instance field (a fixed buffer excluded), or the auto-property
whose backing field it is, recognised from metadata by the `<Name>k__BackingField` name; the declaring type must be able
to read it. A wrapper's member is a number, `bool`, `char`, pointer or enum, not another wrapper, matching Iris.

**Touches.** The Iris fixture is the example component, so the Iris test natives went into
`packages/Iris/Examples/Hosting/ExampleNatives.hpp` and `Examples/Hosting/Component/Natives.cs`, outside the Touches line.
`Iris.Bridge/InternalCalls.cs` needed no change.
