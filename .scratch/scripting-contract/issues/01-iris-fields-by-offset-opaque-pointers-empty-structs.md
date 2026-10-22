# 01: Iris 0.3: fields by offset, opaque pointers, empty structs

**What to build:** the three Iris capabilities the generated mappings need (spec: "Iris changes"; ADR-0006 as amended on 2026-10-22, ADR-0016).

- **`Iris::Field<T>(std::string_view name, std::size_t offset)`** in `Method.hpp`: a public way to declare one mapped field by its C++ type and byte offset, with no member access, giving the same `Detail::StructFieldInfo` that `IRIS_FIELD` gives. `T` may be anything `IRIS_FIELD` accepts (number, `bool`, `char16_t`, mapped enum, mapped struct, `T[N]`/`std::array`) plus the new pointer kind below. `IRIS_FIELD` and `IRIS_FIELD_AS` stay, implemented over it.
- **A pointer-sized opaque field kind.** A struct field of any pointer type (`EntityList*`, `void*`) maps as `ValueKind::Pointer`; the managed field must be `nint`, `nuint`, `IntPtr`, `UIntPtr`, `long` or `ulong` at the same offset (8 bytes; Iris already requires a 64-bit build for this). The bridge never reads through it. `StructMapping.cs` validates the kind and size; `ValueCodec` copies it as 8 bytes.
- **Zero-field structs.** `StructMapping.cs` accepts a description with no field lines when the native size is 1 and the managed struct has no instance fields (whose managed size is also 1). `StructFields()` with no arguments compiles.
- **Version and log.** Package version 0.3.0; the README gains a "Changes" section listing every public change in this release and what it replaces (the amendment's rule), starting with these three. README "Structs" documents `Field`, the pointer kind and empty structs.
- Nothing else in Iris's public headers changes in this ticket.

**Blocked by:** none.

**Touches:** `packages/Iris/Native/include/Iris/Method.hpp`, `packages/Iris/Native/src/`, `packages/Iris/Managed/Iris.Bridge/StructMapping.cs`, `packages/Iris/Managed/Iris.Bridge/ValueKinds.cs`, `packages/Iris/Managed/Iris.Bridge/ValueCodec.cs`, `packages/Iris/Tests/`, `packages/Iris/README.md`, `packages/Iris/CMakeLists.txt` (version).

**Status:** resolved

- [x] Iris tests: a struct mapped only with `Field<T>(name, offset)` validates against its managed mirror; a wrong offset, name or kind fails with a diagnostic naming the field.
- [x] A struct with a `void*` member validates against a managed struct with `ulong` at that offset, and fails against `uint` there; a pointer field's bytes cross unchanged in both directions.
- [x] An empty C++ struct validates against an empty C# struct; a zero-field description against a struct with one field fails naming it; a zero-field description whose size is not 1 fails.
- [x] The existing Iris tests, FenrirScripting's and FenrirPhysicsScripting's pass unchanged.

**Resolved by:** branch claude/scripting-contract-01-iris-fields, into claude/physics-vehicles-spec. `Iris::Field<T>(name, offset)` sits beside `StructFields` in `Method.hpp` and takes over the body of `Detail::MakeField`, which is gone; `IRIS_FIELD` and `IRIS_FIELD_AS` expand to it. A pointer `T` gives `ValueKind::Pointer` (the existing 17, until now only for internal calls), with a `static_assert` that it is 8 bytes. On the managed side `StructMapping.cs` accepts a Pointer field held as `nint`, `nuint`, `long` or `ulong` and refuses anything else, naming the field. `nint`/`nuint` fields now count as blittable. A struct with no fields is no longer refused as "has no fields". A description with no field lines is checked before the size, so a managed struct with fields fails naming its first field, and a native size other than 1 fails as such. `ValueCodec` needed no code change: struct copies are already whole-byte copies, so only its comment says so. Iris is 0.3.0; the README gains "Changes" and three bullets under "Blittable structs", and its freeze paragraph and `IMPLEMENTATION_PLAN.md` now follow ADR-0006 as amended. Tests: three `Components` cases (offset-only mapping, opaque pointer, empty structs) and new `StructMappingChecks` cases. `clang-x64-debug` with `IRIS_BUILD_TESTS=ON` passes 94/94, and without it 81/81. Point for review:
- **`long` and `ulong` match a pointer, but `nint` does not match an `Int64` field.** `nint` maps only as a pointer, so an `int64_t` member still needs a managed `long`.
