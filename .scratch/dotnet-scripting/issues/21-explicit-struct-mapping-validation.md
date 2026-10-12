# 21: Explicit struct-mapping validation

**What to build:** A public, additive call that validates a `StructMapping<T>` against a managed type in a loaded component without calling any method, such as `Component::ValidateStruct<T>()` returning `CallResult<void>`. Today a mapping is validated only the first time it is used as a method argument, return or field. Internal calls pass structs by pointer and never check the layout. The engine shares component memory with C# through pointers, so it needs every mapping validated when a component loads, or a layout mismatch corrupts memory silently.

**Blocked by:** none

**Status:** resolved

- [x] The call validates name, size, field count and each field's name, offset and kind, including nested structs, with the same rules and diagnostics as implicit validation.
- [x] Success is cached exactly like implicit validation, so later uses don't validate again, and calling it twice is harmless.
- [x] A missing type, a non-blittable type or any mismatch returns a failure status with a diagnostic naming the field, and leaves the component usable.
- [x] The handle and generation rules apply: a released component returns `InvalidHandle`.
- [x] Tests cover a match, each kind of mismatch, a nested struct and a released component.

## Comments

### Resolution

- **API:** `Component::ValidateStruct<T>()` returns `CallResult<void>`. It is a thin native call: it registers the mapping (and any nested structs, enums and array element types) exactly as the first implicit use does, so no bridge change was needed and the bridge version is unchanged.
- **Caching:** the registration is the cache. The managed side hands back the same struct handle for a type it has already accepted, so a later lookup that uses the struct, or a second call here, is harmless.
- **Failures:** a missing type is `NotFound`; a mismatch or non-blittable type is `InvalidArgument` with the field named; a released or moved-from component is `InvalidHandle`. The component stays usable after a failure.
- **Tests:** one case in `components.cpp` covers a match, twice, nested/enum/array mappings, a size mismatch, a field-order mismatch, a nested mismatch, a non-blittable type, a missing type, a field-named array mismatch, use after failures, and a released component.


Filed from the ticket 19 grilling session (engine-scripting spec, Q14 and Q20). It blocks engine-scripting ticket 04.
