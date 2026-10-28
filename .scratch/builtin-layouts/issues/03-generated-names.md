# 03: The generated types' names are spelled once per side

**What to build:** the seven names C++ finds in a script assembly (the system host and its four methods, the two report tables and `Register`) live in one C++ header and one generator constants class (spec: "(c) The generated-type names are concentrated, not shared").

- **`packages/FenrirScripting/src/SystemHostNames.hpp` → `src/GeneratedNames.hpp`** (`git mv`), namespace `Fenrir::Detail::GeneratedNames`. It keeps `Type` renamed `SystemHost`, and `PhaseMask`, `OnCreate`, `Run`, `OnDestroy`, and gains `constexpr std::string_view ComponentTable = "Fenrir.Scripting.Generated.ComponentTable";`, `EventTable = "Fenrir.Scripting.Generated.EventTable";` and `Register = "Register";`, each with a one-line doc. The header comment says: these are the names of the types `Managed/Fenrir.Scripting.Generators/` writes (`GeneratedNames.cs` there spells the same names), that a script assembly whose generated types do not match fails to start, and that the end-to-end tests (`Tests/SystemTests.cpp`, `Tests/AdmissionTests.cpp`, every test that loads a generated assembly) pin them. `packages/FenrirScripting/CMakeLists.txt` lists the header if it lists `SystemHostNames.hpp` today.
- **`packages/FenrirScripting/src/ScriptData.cpp`** `IrisScriptAssembly::Report`: the `tables[]` initialiser uses `GeneratedNames::ComponentTable` and `GeneratedNames::EventTable` (the `Table` struct's `type` becomes `std::string_view` if `FindType` takes one; check the signature `Scripting.cpp:177` already calls it with), and `FindMethod<void()>(GeneratedNames::Register)`. Include the header.
- **`packages/FenrirScripting/src/Scripting.cpp`**: the include at (today) line 9 and the five qualified names at lines 167 (comment) and 177 to 190 follow the rename (`Detail::GeneratedNames::SystemHost` and the four methods). Nothing else in the file changes: candidate 4's spec edits `Install` in this file, so keep this diff to those six lines and rebase if it lands first.
- **`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/GeneratedNames.cs`** (new): `internal static class GeneratedNames` with `const string Namespace = "Fenrir.Scripting.Generated"`, `SystemHost = "SystemHost"`, `ComponentTable = "ComponentTable"`, `EventTable = "EventTable"`, `Register = "Register"`, `PhaseMask`, `OnCreate`, `Run`, `OnDestroy`, and a helper or three constants for the hint names (`Namespace + "." + SystemHost + ".g.cs"` and the two tables). Its comment points at `packages/FenrirScripting/src/GeneratedNames.hpp` and says the two are kept equal by the end-to-end tests.
- **`ComponentFieldsGenerator.cs`** (lines 76 to 82), **`EventGenerator.cs`** (58 to 63) and **`SystemGenerator.cs`** (80 to 96): the `namespace`, class and method lines and the `AddSource` hint names are built from `GeneratedNames`. `SystemGenerator.cs`'s comment about `SystemHostNames.hpp` names `GeneratedNames.hpp`. The doc-comment mentions at `EventGenerator.cs:13`, `SystemGenerator.cs:12` and `Managed/Fenrir.Scripting/Systems.cs:110` stay prose. `Tests/BadReports/BadReports.cs` stays as it is: it is a hand-written stand-in for the tables and says so.
- **`docs/adr/0042-...md`** already carries the amendment sentence (applied by the spec's PR); the ticket changes no ADR. `packages/FenrirScripting/README.md`: update any mention of `SystemHostNames.hpp`.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/src/GeneratedNames.hpp` (renamed from `SystemHostNames.hpp`), `packages/FenrirScripting/src/ScriptData.cpp`, `packages/FenrirScripting/src/Scripting.cpp` (lines 9 and 167 to 190 only; overlaps candidate 4's spec), `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/GeneratedNames.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/ComponentFieldsGenerator.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/EventGenerator.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/SystemGenerator.cs`, `packages/FenrirScripting/README.md`

**Status:** claimed

- [x] `packages/FenrirScripting/src/SystemHostNames.hpp` does not exist; `grep -rn "Fenrir.Scripting.Generated" packages/FenrirScripting/src packages/FenrirScripting/Managed/Fenrir.Scripting.Generators --include=*.cpp --include=*.hpp --include=*.cs` finds a string literal only in `GeneratedNames.hpp` and `GeneratedNames.cs` (doc comments excepted).
- [x] The generated `.g.cs` text is byte-for-byte what it was (compare a fixture's generated output before and after, or rely on the end-to-end tests passing with no fixture change).
- [x] The scripting tests pass under the msvc-x64-debug preset; `Tests/Rules/ScriptSystemsTests.cpp` and `Tests/Rules/FakeSystemHost.hpp` compile unchanged (they name the `SystemHost` seam, not the names header).
- [x] `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-28 in the spec worktree, stacked on the spec branch. `Table::type` became `std::string_view` for `FindType`. The README had no mention of the old header. msvc-x64-debug: 17/17 scripting tests, every fixture built by the changed generators.


Filed from the builtin-layouts grilling (spec decisions 5, 7). Cut from `dotnet-scripting`; runs side by side with 01 and 02.
