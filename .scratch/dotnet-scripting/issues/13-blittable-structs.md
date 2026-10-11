# 13: Blittable structs

**What to build:** Native structs whose layout matches a managed blittable struct (for example a `Vec3`) can be passed as arguments, returned, and used as field/property values. Layout mismatches are detected and diagnosed, and never silently corrupt memory.

**Blocked by:** 07

**Status:** resolved

- [x] Native code can declare a mapping between a C++ struct and a managed struct type.
- [x] On first use, size and field layout are validated against the managed type. A mismatch returns a diagnostic and the call never happens.
- [x] Structs round-trip as arguments and return values. Nested blittable structs work.
- [x] Non-blittable managed structs are rejected with a clear diagnostic.
- [x] Tests cover a matching struct, a size mismatch and a field-order mismatch.

## Comments

### Resolution

- Native: `Iris::StructMapping<T>` specialisation (`typeName`, `fields` from `IRIS_FIELD`), concept
  `StructValue<T>`. Structs are legal signature arguments/returns and `Member<T>` values.
- Wire: kind 14 (`Struct`) with the bridge's struct handle in bits 8+ of the 32-bit kind. New export
  `RegisterStruct` (text description: `Type|size` then `name|offset|kind|nested` per field) validates
  against the managed type and returns the handle; native registers nested mappings first. Kinds for
  lookups are resolved at `FindMethod`/`FindField`/`CreateObject` time, so a bad mapping fails before any
  call. Table slot 21, bridge version 18.
- Argument slot = pointer to the caller's struct bytes (borrowed); return slot = bridge-allocated block
  of the struct's bytes, copied once and given back with `FreeString`, with the same cleanup guard as
  strings if completing the call throws.
- Known limits: `bool`/`char`/enums are not blittable here (a later ticket could add enums); a mapping is
  re-validated on every lookup that uses it (one cheap bridge call per struct per lookup); a struct that
  is only wrong in a way the described fields cannot show (e.g. different field *names* with identical
  layout) is caught by name, but padding bytes are not compared.
