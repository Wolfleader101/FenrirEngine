Status: ready-for-agent

# Rules harness: a native's rules are tested through its own registration, and the generated tests stand on public interfaces

Origin: the third architecture review of `dotnet-scripting` (2026-10-28), candidate 1 (the rules tests call a native through its registration; the review's top recommendation) and candidate 7 (the generated defaults tests off `Iris::Detail`, one shape of test app, the thunks out of the public header), grilled together on 2026-10-28 to eleven decisions, every recommendation accepted. The two were combined because both reshape the same test harnesses and the same header.

The vocabulary is the repo's: *native function*, *call context*, *extension state*, *native guard*, *scripting extension*, *binding*, *tagged union*, *script assembly* from `GLOSSARY.md`, and *native call harness* (named by this spec); *module*, *interface*, *seam*, *adapter*, *depth*, *locality*, *leverage*, *the deletion test* from the codebase-design skill.

## Problem statement

**The rules tests re-spell the registration by hand.** The six package rules tests (`packages/FenrirPhysics/Scripting/Tests/Rules.cpp`, `FenrirWindow/.../Rules.cpp`, `FenrirInput/.../NoInput.cpp`, `FenrirAssets/.../NoOps.cpp`, `FenrirRenderer/.../NoOps.cpp`, `FenrirDebugDraw/.../NoOps.cpp`, 1492 lines) call each native function bare, as the generated natives header declares it, with a call context from `NativeCallHarness::Context("Pkg.Native")` (73 hand-typed names) and an extension state the test makes. A native with outputs is called through `ZeroingOutputs<&Native, i...>::Call` with the index list copied from the generated registration (14 copies). The thunk the registration installs (`Detail::NativeFunctionCall` in `include/FenrirScripting/ScriptingExtension.hpp`: build the context from the active Scripting, answer `notRunning` when none runs, pass the extension state) runs only under .NET. Deleting the harness would put every rule back on .NET: the harness concentrates, which is the signal; but what it concentrates is a second, hand-written spelling of the registration.

**Each file owns the warning format.** `WarnOnceRecord` logs `[<extension>] <Native.Method>: <message> (warned once)`. The six rules tests and FenrirScripting's own `Tests/Rules/NativeGuardTests.cpp` each define `once` (one in a debug build, zero in release), `Warnings(harness, text)` and `Logged(native, message)`, re-spelling that format seven times outside the record, and copy twelve of the natives' eighteen warning texts as string constants (eight texts sit on the extension state headers, ten inline in the natives' sources, none named).

**The generated defaults tests reach into Iris.** The eight generated `*DefaultsTests.cpp` (1866 lines, from the `Comparison` block in `packages/Iris/Tools/Iris.Bindings/src/CppWriter.cs`) each carry a 130-line field-wise comparison that names `Iris::Detail::StructInfo`, `ValueKind` and `StructInfoOf`, the only `Iris::Detail` use outside Iris: a change to Iris's mapping record recompiles seven packages' tests, and the walk of Iris's own record is written by a tool outside Iris.

**Seven test apps in six namespaces, one of them hand-written twice.** `packages/FenrirScripting/Tests/FixtureApp.hpp` (46 lines) is `ExtensionTestApp<NoPackages>` written out again, used only by the two built-in bindings' generated tests. The six `<Pkg>ScriptingApp.hpp` sit in `Fenrir::Testing` (physics) and five `<Pkg>ScriptingTests` namespaces, each re-exporting the shared names its tests use unqualified, which is why the seven bindings' `[tests]` sections differ in more than the package name.

**The thunks sit in the public header.** `ScriptingExtension.hpp` holds about 95 lines of `Detail::` thunk and traits templates, the bodies of `NativeFunction::Make`, and `#include <Iris/Iris.hpp>` for them; every package's public `<Pkg>Scripting.hpp` includes it, so Iris's headers and the thunk reach every translation unit that names a package's extension (33 files include it directly).

## Solution

### The native call harness runs the extension as shipped

`Fenrir::Testing::NativeCallHarness` (`packages/FenrirScripting/Tests/Support/`) gains a constructor taking the scripting extensions to run, `NativeCallHarness harness(Physics::ScriptingExtension());` (several, as `ScriptingConfig::extensions` lists them). It registers each extension's native functions as `Scripting::Install` does, through `NativeFunction::Register` with the extension's state, so each thunk's name, state and not-running value are set; and it activates its own engine context (its app, its empty registry, its change buffer) for its lifetime, deactivating it in its destructor. The test runs the extension the package's public function returns, by-hand additions and generated maker alike, and no longer makes an extension state or includes the natives header.

Iris's `RegisterInternalCallRaw` stores a registration and hands it to the bridge only once one is up, so registering needs no .NET; the active context is one process-wide atomic, so one harness is active at a time, as one Scripting is, and a second constructed while one lives fails naming it. A registration that fails (a state of the wrong type, a name Iris refuses) throws `std::runtime_error` naming the native, which Catch2 reports. `NativeFunction` and `ScriptingExtension` name `Testing::NativeCallHarness` a friend, as `CallContext` already does; Install's own registration stage is untouched (candidate 4's spec, under grilling in a sibling session, may later put a seam there that both Install and the harness use; the two adapters will justify it then).

**Calling by registered name.** `harness.Call<bool>("PhysicsNative.AddImpulse", &body, &Up)`: the result type is explicit, the parameter types are the arguments' decayed types. `NativeFunction` carries, beside its registrar, the thunk's pointer and a `std::type_index` of the thunk's signature `R(Args...)` (the C# signature: context and state dropped, which is what `NativeFunctionCall::Call` already has), both set by `Make`. The harness finds the function by the type-and-method part of its name (`NativeFunction::NativeNameFor`), compares `typeid(R(Args...))` with the recorded index, and calls through the pointer; a mismatch is a `std::runtime_error` naming the native and both signatures, never a bad cast. A literal `0` where a native takes `std::uint64_t`, or `"text"` where it takes `std::string_view`, fails loudly and the test writes the type (`std::uint64_t{0}`, `"text"sv`), or names the parameter types explicitly (`Call<void, std::string_view>(...)`). The zeroing of outputs is the registration's (ADR-0045), so the index lists vanish from the tests. `Context(native)` stays for FenrirScripting's own Rules tests of the guard, the marshalling and the zeroing template, which call no registered native.

**One assertion for "warned once".** `WarnOnceRecord` gains a static `Line(native, message)` returning the whole text after the `[<extension>] ` prefix, which `Warn` itself logs, so the format is spelled once. The harness gains `Warnings(native, message)`, the number of warnings whose text ends with that line, `once` (one in a debug build, zero in release, from `NDEBUG`), and `WarnedOnce(native, message)`, true when `Warnings` equals `once`. A test writes `REQUIRE(harness.WarnedOnce("PhysicsNative.AddImpulse", PhysicsScripting::NoWorld));`. The extension prefix is not matched: it is the record's constructor argument, pinned once in `NativeGuardTests`. The seven files delete `once`, `Warnings` and `Logged`.

**The texts are named where the rules are.** Every warning text a package's natives log becomes a named `static constexpr std::string_view` on the package's extension state header (`PhysicsScripting::NoWorld`, `NoBody`, `NoJoint`, …), the inline ones in the natives' sources included, so a package's rules read in one place; a text with a variable part becomes a function there returning the text (`AssetResolve::StaleHandle(kindName)`). The guard's own text is `NativeGuard::NotAlive`; Assets' two on `AssetResolve.hpp` stay public, since the renderer reuses them. The rules tests name the constants and copy nothing; they keep `../src` on their include path for the state header, the one private thing they still see.

### The generated tests stand on public interfaces

**Iris owns the field-wise walk.** Iris gains one public header, `Iris/FieldDifferences.hpp`, with `Iris::FieldDifferences<T>(const T& left, const T& right, std::span<const Iris::UnionLayout> unions = {}) -> std::vector<std::string>`: the bytes of each field of `T`'s mapping at its offset, recursing into nested structs and array elements, never padding, each difference spelled as the field's path, offset and both sides' bytes; of a tagged union only the tag and the kind it selects, `UnionLayout` naming the union's type and, per tag value, the field it selects (which field a tag selects is the binding's knowledge, so it stays a generated input). Iris tests it in its own test suite over a small struct and a union. The generated test keeps the tagged-union table, `CheckDefaults` (the C# call and the Catch2 asserts, about 35 lines) and its cases; the `Comparison` block leaves `CppWriter.cs`; `Iris::Detail` is named nowhere outside Iris, and Iris.Bindings' own fixture test uses the same function.

**The thunks leave the public header.** `Detail::NativeFunctionCall`, `NativeFunctionTraits`, `StateTypeOf` and the bodies of both `NativeFunction::Make` overloads move to a new `FenrirScripting/NativeFunctionMake.hpp`. `Make` stays spelled `NativeFunction::Make`, declared in the class and defined in the new header, so ADR-0045, ADR-0046, the glossary and the `[natives] make` key are untouched. The new header is named in the defaults file's `[natives] includes` (`packages/FenrirScripting/api/FenrirScripting.binding-defaults.toml`), so every generated natives header carries it and every generated registration compiles; the hand-written makers (`src/Bindings.cpp`, `Tests/TestExtension.cpp`, `Tests/AdmissionTests.cpp`, `Tests/Rules/ZeroingOutputsTests.cpp`) include it by hand. `ScriptingExtension.hpp` keeps `CallContext`, `ChangeBuffer`, `NativeFunction`'s data, name rules and private registrar, `ScriptingExtension`, and swaps `<Iris/Iris.hpp>` for `<Iris/CallResult.hpp>`.

**Seven test apps of one shape, in one namespace.** `FixtureApp.hpp` becomes `packages/FenrirScripting/Tests/ScriptingApp.hpp`: `Fenrir::Testing::ScriptingApp : ExtensionTestApp<NoPackages>` reading `FENRIR_TEST_FIXTURE`, about twelve lines, used by both built-in bindings' generated tests. The six `<Pkg>ScriptingApp.hpp` move into `Fenrir::Testing` with their helper constants and functions, dropping the re-exporting `using` declarations, and the tests that named `<Pkg>ScriptingTests::` follow. The seven `[tests]` sections then differ only in the package name: `setup = "Fenrir::Testing::<Pkg>ScriptingApp app;"`. The keys stay explicit (decision 9): six lines per binding is the convention made visible, and deriving them would put a placeholder language and two exceptions (the two built-in bindings share one fixture; a binding with nothing registered has no test) into a generic tool for 42 lines. The C# fixture namespaces are left as they are.

### Out of scope

- Install's registration stage and the extension set behind a seam (candidate 4, its own spec).
- The not-running path (`notRunning`) under the harness: the harness is always the active Scripting; FenrirScripting's .NET tests keep that rule.
- Deriving the `[tests]` keys or any other binding key from the package name (rejected, decision 9).
- Renaming the C# fixture namespaces.
- The physics test's separate "prefixed by the extension" check, which goes with `Logged`.

## Decisions (from the grilling)

1. The harness takes the extensions the packages' public functions return, registers their natives as Install does, and is the active Scripting while it lives.
2. A native is called by its registered name with the result type explicit and the parameter types deduced; `NativeFunction` carries the thunk and its signature's `type_index`, checked at run time.
3. `WarnOnceRecord::Line` spells the format once; the harness owns `Warnings`, `once` and `WarnedOnce`; the seven files' `once`, `Warnings`, `Logged` go.
4. Every warning text is a named constant (or a function, when it has a variable part) on the package's extension state header; the guard's on `NativeGuard`; Assets' two on `AssetResolve.hpp`.
5. `NativeFunction` and `ScriptingExtension` befriend the harness; no new registration module here.
6. Iris gains `Iris::FieldDifferences` and `Iris::UnionLayout` in `Iris/FieldDifferences.hpp`; the generated test keeps `CheckDefaults` and the union table.
7. `FixtureApp` becomes `ScriptingApp : ExtensionTestApp<NoPackages>` in `ScriptingApp.hpp`.
8. All seven test apps in `Fenrir::Testing`.
9. The `[tests]` keys stay explicit and uniform.
10. The thunks and `Make`'s bodies move to `NativeFunctionMake.hpp`, named in the defaults file's `[natives] includes`; the public header drops `Iris/Iris.hpp`.
11. One spec, five tickets.
12. `GLOSSARY.md` gains *native call harness*; no ADR.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `Iris::FieldDifferences` and its test; `CppWriter.cs` writes `CheckDefaults` over it; the eight generated tests regenerated | none |
| 02 | `NativeFunctionMake.hpp`; `ScriptingExtension.hpp` without the thunks or `Iris/Iris.hpp`; the defaults file and the hand-written makers | none |
| 03 | The harness runs extensions and calls by name; `NativeFunction` carries the thunk and its signature; `WarnOnceRecord::Line`, `Warnings`, `once`, `WarnedOnce`; `NativeGuard::NotAlive`; `NativeGuardTests` onto them | 02 |
| 04 | The six rules tests onto the harness; every warning text a named constant on its state header; `AssetResolve` constants | 03 |
| 05 | `ScriptingApp.hpp`; the six apps into `Fenrir::Testing`; the seven `[tests]` sections uniform; the generated tests regenerated | 01 |

Branches `rules-harness-01-field-differences`, `-02-thunk-header`, `-03-harness-by-name`, `-04-rules-tests`, `-05-one-test-app`. 02, 03 and 04 are a stack (each cut from the previous, each PR targeting it). 01 is cut from `dotnet-scripting`; 05 is cut from 01's branch and targets it. 01 and 05 touch no file that 02 to 04 touch, so the two lines run side by side; the generated outputs they regenerate are different files of the same `fenrir-generate-bindings` run, and whichever line merges second re-runs it.

## Done looks like

- `grep -rn "Iris::Detail" packages tools --include=*.cpp --include=*.hpp | grep -v "packages/Iris/"` finds nothing; `packages/Iris/Native/include/Iris/FieldDifferences.hpp` exists and an Iris test exercises it.
- `grep -n "Iris/Iris.hpp\|NativeFunctionCall\|NativeFunctionTraits" packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp` finds nothing; `NativeFunctionMake.hpp` exists and is in the defaults file's `[natives] includes`.
- `grep -rn "ZeroingOutputs<\|harness.Context(\|warned once\|constexpr std::size_t once" packages/*/Scripting/Tests` finds nothing; `grep -rn "warned once" packages --include=*.hpp --include=*.cpp` finds only `WarnOnceRecord.hpp`.
- `grep -rn "^namespace" packages/*/Scripting/Tests/*ScriptingApp.hpp packages/FenrirScripting/Tests/ScriptingApp.hpp` finds only `Fenrir::Testing`; `FixtureApp.hpp` does not exist; every `[tests] setup` reads `Fenrir::Testing::<Pkg>ScriptingApp app;`.
- `ctest -N` lists the same test names as before; every scripting, bindings and Iris test passes under the msvc-x64-debug preset after each ticket; the other presets are CI's.
