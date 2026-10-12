# 20: bool, char, enums and inline arrays across the boundary

**What to build:** Additive support (ADR-0006) for value types that mapped structs and internal calls reject today. Game components routinely hold flags and enum states. The engine's `Name` component is a fixed `char[256]`. Engine scripting (`.scratch/engine-scripting/`) cannot expose real components without these.

**Blocked by:** none

**Status:** resolved

- [x] `bool` works as a mapped-struct field, an internal-call parameter or return, and a method argument or return. The managed and native representations are documented as one byte, and an invalid byte value has defined behaviour.
- [x] `char` (UTF-16 `char` ↔ `char16_t`) works in the same places. `char`/`char8_t` arrays are covered by the inline-array criterion.
- [x] Enums (review-18 G3) work as fields, parameters and returns. The underlying type must match exactly, and a mismatch fails validation with a diagnostic.
- [x] Fixed-size inline arrays of a supported element type work as mapped-struct fields (C++ `T[N]`/`std::array<T, N>` ↔ C# `fixed` buffer or `[InlineArray(N)]`), and the length is validated.
- [x] Struct-mapping validation (name, offset, kind, size) covers each new kind.
- [x] Tests cover each kind both ways, plus each mismatch case.

## Comments

Filed from the ticket 19 grilling session (engine-scripting spec, Q14 and Q20). It blocks engine-scripting tickets 04 and 05.

### Resolution

- **Struct bytes are copied directly.** The bridge no longer uses the marshaller (`Marshal.SizeOf`, `OffsetOf`, `PtrToStructure`) for mapped structs. `ManagedLayout` reads the size (`Unsafe.SizeOf`) and each field's offset (a small `DynamicMethod`) from the runtime and copies the bytes. The marshaller gives `bool` four bytes, and it is unusable in an assembly with `DisableRuntimeMarshalling`. The example component now has that attribute.
- **bool:**
  - It is one byte in memory.
  - It works as a mapped-struct field and as an internal call parameter or return, as well as the method and member forms that already existed.
  - Defined behaviour for an invalid byte: any nonzero byte reads as true and is stored as 1. This covers struct fields (including nested structs and inline arrays), single values and bulk array copies.
- **char:**
  - Wire kind `Char` (18). C# `char` is `char16_t`, and it works in the same places as `bool`, including `Array<char16_t>`.
  - `char`/`char8_t` are not scalars; they are accepted only as inline-array elements, where they are bytes (a C# `byte`).
- **Internal calls with bool or char require `[assembly: DisableRuntimeMarshalling]`.** Otherwise the runtime picks the function pointer's `bool`/`char` size, which would not match C++. The load fails with a diagnostic naming the field. A check in `InternalCallChecks` proves it against an assembly without the attribute.
- **Enums:**
  - `EnumMapping<E>` (`typeName`) is registered like a struct (same handle space, wire kind `Enum` (19), one description line `TypeName|size|underlyingKind`).
  - The underlying type must match exactly.
  - Enums work as method arguments and returns, as fields and properties, and as mapped-struct fields.
  - Not supported: enum array elements (not asked for).
- **Inline arrays:**
  - Struct fields `T[N]` and `std::array<T, N>` mirror a C# fixed buffer or an `[InlineArray(N)]` struct.
  - Wire kind `InlineArray` (20), with a six-part field line carrying the element kind and the length.
  - Elements can be numbers, `bool`, `char16_t`, mapped structs and enums, plus `char`/`char8_t` as bytes.
  - The offset, the length, the element kind and the nested type name are checked.
- **Tests:**
  - `components.cpp`: one case each for bool, char, enums and inline arrays, covering both directions, the invalid-byte case and the mismatches (wrong underlying type, wrong enum field, wrong length, wrong element type).
  - `StructChecks`: the wire-level descriptions for each new kind.
  - `InternalCallChecks`: bool and char internal calls, and the marshalling requirement.
  - Passing in Debug and Release (12/12).
- **Docs:** the README type table, the struct section and the internal call section describe all of it.
- **Wire only:** the new kinds are additive on the private bridge wire format, so the bridge version is unchanged.
