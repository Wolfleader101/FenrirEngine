# 01: `Iris::FieldDifferences`: Iris walks its own mapping, and the generated defaults test calls it

**What to build:** the field-wise comparison the generated defaults tests carry moves into Iris as one public function, so the generated test names nothing in `Iris::Detail` (spec: "Iris owns the field-wise walk").

- **`packages/Iris/Native/include/Iris/FieldDifferences.hpp`** (new, public): `Iris::UnionLayout { std::string_view typeName; std::vector<UnionKind> kinds; }` with `UnionKind { std::uint64_t tag; std::size_t field; }`, and `template <StructValue T> std::vector<std::string> FieldDifferences(const T& left, const T& right, std::span<const UnionLayout> unions = {})`. The body is today's `Comparison` from `packages/Iris/Tools/Iris.Bindings/src/CppWriter.cs` (the `Comparison` string array: `ScalarSize`, `Hex`, `Bytes`, `Unsigned`, `Value`, `Struct`, the inline-array loop), over `Detail::StructInfoOf<T>`, in `Iris::Detail` helpers the header keeps private. Each difference reads `<path> at offset <n>: left <hex>, right <hex>`, the path starting at the mapping's type name. The header is listed with the others in Iris's README and included by `Iris/Iris.hpp` only if the other public headers are; keep whatever the umbrella's rule is.
- **`packages/Iris/Tests/`**: one test source (`field_differences.cpp`, added to `Tests/CMakeLists.txt` as the others are) over a struct with a scalar, a nested struct, an inline array and an enum, and a tagged union with two kinds: equal values give no differences; one scalar changed gives one line naming its path and offset; padding bytes that differ give none; of the union only the tag and the selected kind are compared.
- **`CppWriter.cs`**: the `Comparison` array goes; the generated test includes `<Iris/FieldDifferences.hpp>`, keeps the `taggedUnions` table as `std::vector<Iris::UnionLayout>` (same values), and `CheckDefaults<T>` calls `Iris::FieldDifferences(native, managed, taggedUnions)` and asserts the vector is empty with the same `INFO` ("These fields of C#'s new() differ from C++'s T{}:" plus each line). The generated test's includes drop `<cstring>` and `<span>` if nothing left needs them. `tests/BindingTests.cs` keeps passing (it looks for `CheckDefaults<Fixture::BodyId>`); add one assertion that the output names `Iris::FieldDifferences` and not `Iris::Detail`.
- **Regenerate** the eight `*DefaultsTests.cpp` (`cmake --build <dir> --target fenrir-generate-bindings`, and the Iris.Bindings fixture's by its own test run); each shrinks by about 130 lines and names no `Iris::Detail`.
- **README**: `packages/Iris/README.md` lists the header; `packages/Iris/Tools/Iris.Bindings/README.md`'s "The defaults test" paragraph says the comparison is Iris's.

**Blocked by:** none

**Touches:** `packages/Iris/Native/include/Iris/FieldDifferences.hpp`, `packages/Iris/Native/include/Iris/Iris.hpp`, `packages/Iris/Tests/`, `packages/Iris/README.md`, `packages/Iris/Tools/Iris.Bindings/src/CppWriter.cs`, `packages/Iris/Tools/Iris.Bindings/tests/`, `packages/Iris/Tools/Iris.Bindings/README.md`, `packages/*/Scripting/Tests/Generated/*DefaultsTests.cpp`, `packages/FenrirScripting/Tests/Generated/*DefaultsTests.cpp`

**Status:** claimed

- [x] `grep -rn "Iris::Detail" packages tools --include=*.cpp --include=*.hpp | grep -v "packages/Iris/"` finds nothing.
- [x] The Iris test, Iris.Bindings' tests and every `*Scripting` CTest pass under the msvc-x64-debug preset.
- [x] `check_tickets.py` and clang-format pass.

## Comments

Filed from the rules-harness grilling (spec decision 6). Cut from `dotnet-scripting`; ticket 05 is cut from this branch.
