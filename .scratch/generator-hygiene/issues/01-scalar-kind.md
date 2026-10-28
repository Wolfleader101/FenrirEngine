# 01: One scalar classifier in describe-api

**What to build:** describe-api classifies a scalar type in one place, so a `char` is `u8` in a field and in a native alike and the committed natives descriptions no longer depend on the host's signedness (spec: "One scalar classifier").

- **`tools/describe-api/src/Describer.cs`**: `internal static string? ScalarKind(Type type)` returning `bool`, `char16`, `u32` (for `char32_t`), `f32`, `f64`, `u8` (for `CXType_Char_S` and `CXType_Char_U`, with the existing "a byte of text, unsigned on every target" comment moved onto it) or `IntegerKind(type)`'s answer, else null. The field classifier's switch calls it first and keeps its handle-enum, enum, record, pointer and array cases.
- **`tools/describe-api/src/Natives.cs`**: the parameter and result classifier calls `ScalarKind` and wraps its answer in `NativeType(alias ?? spelling, kind)`, where the spelling is what the switch gives today (`bool`, `char16_t`, `char32_t`, `float`, `double`, `char`, `FixedWidth(kind)` for an integer); it keeps `void`, `std::string_view`, records, enums, pointers and the reference error. The `CXType_Char_S`/`CXType_Char_U` case and the trailing `IntegerKind` fallthrough go.
- **`packages/Iris/docs/natives-description.md`**: the `kind` row says a `char` is `u8` on every target, as `api-description.md` already says.
- **Regenerate** with `fenrir-describe-api`: `packages/FenrirScripting/api/FenrirScripting.natives.json` changes its eighteen `"i8"` entries to `"u8"`; `tools/describe-api/tests/Fixture/Fixture.natives.json` changes if its natives take `char` (check and commit). Then `fenrir-generate-bindings`: a no-op, since Iris.Bindings reads the type name for text; if any generated file changes, stop and say what.

**Blocked by:** none

**Touches:** `tools/describe-api/src/Describer.cs`, `tools/describe-api/src/Natives.cs`, `tools/describe-api/tests/`, `packages/Iris/docs/natives-description.md`, `packages/FenrirScripting/api/FenrirScripting.natives.json`

**Status:** claimed

- [x] `DescribeTests.cs`: a case describes a native taking `const char*` with a count and one taking a `char` by value and reads `u8` for both; the fixture's byte-for-byte case still passes.
- [x] `grep -n "CXType_Char_S" tools/describe-api/src/*.cs` finds one line; `grep -c '"i8"' packages/FenrirScripting/api/FenrirScripting.natives.json` is 0. (Built: the grep finds three lines, each a different question: `ScalarKind`'s `u8`, `IntegerKind`'s signed list, which an enum's underlying type still needs, and the natives spelling `char`, which `u8` alone cannot give back since `std::uint8_t` is a `u8` too. The kind is decided once.)
- [x] `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after the commit; the msvc-x64-debug preset builds and the scripting tests pass.

## Comments

Filed from the generator-hygiene grilling (spec decisions 2, 6). Cut from `dotnet-scripting`; runs beside 02 and 03.
