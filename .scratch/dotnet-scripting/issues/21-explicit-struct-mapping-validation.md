# 21: Explicit struct-mapping validation

**What to build:** A public, additive call that validates a `StructMapping<T>` against a managed type in a loaded component without calling any method, such as `Component::ValidateStruct<T>()` returning `CallResult<void>`. Today a mapping is validated only the first time it is used as a method argument, return or field. Internal calls pass structs by pointer and never check the layout. The engine shares component memory with C# through pointers, so it needs every mapping validated when a component loads, or a layout mismatch corrupts memory silently.

**Blocked by:** none

**Status:** ready-for-agent

- [ ] The call validates name, size, field count and each field's name, offset and kind, including nested structs, with the same rules and diagnostics as implicit validation.
- [ ] Success is cached exactly like implicit validation, so later uses don't validate again, and calling it twice is harmless.
- [ ] A missing type, a non-blittable type or any mismatch returns a failure status with a diagnostic naming the field, and leaves the component usable.
- [ ] The handle and generation rules apply: a released component returns `InvalidHandle`.
- [ ] Tests cover a match, each kind of mismatch, a nested struct and a released component.

## Comments

Filed from the ticket 19 grilling session (engine-scripting spec, Q14 and Q20). It blocks engine-scripting ticket 04.
