# 13: Blittable structs

**What to build:** Native structs whose layout matches a managed blittable struct (for example a `Vec3`) can be passed as arguments, returned, and used as field/property values. Layout mismatches are detected and diagnosed, and never silently corrupt memory.

**Blocked by:** 07

**Status:** ready-for-agent

- [ ] Native code can declare a mapping between a C++ struct and a managed struct type.
- [ ] On first use, size and field layout are validated against the managed type. A mismatch returns a diagnostic and the call never happens.
- [ ] Structs round-trip as arguments and return values. Nested blittable structs work.
- [ ] Non-blittable managed structs are rejected with a clear diagnostic.
- [ ] Tests cover a matching struct, a size mismatch and a field-order mismatch.
