# 11: Fields and properties

**What to build:** Native code can read and write static and instance fields and properties of the supported value types (and of object handles once ticket 10 is done) through validated, cached member handles.

**Blocked by:** 09

**Status:** resolved

- [x] Get and set work for static and instance fields and properties of blittable primitive types.
- [x] Type mismatches, missing members, read-only targets and wrong static/instance usage return diagnostics.
- [x] A property getter or setter that throws returns `ManagedException`, and the object stays usable.
- [x] Member handles are generation-bound and stale-checked, and do not keep the component alive.
- [x] Tests cover round trips and every failure case above.

## Comments

**Done** (11/11 CTests in Debug and Release on Windows). Bridge table version 16 (`FindMember`, `GetMember`, `SetMember`).
- `Type::FindField<T>` and `Type::FindProperty<T>` return a `Member<T>`; `Get()`/`Set(value)` for static members, `Get(object)`/`Set(object, value)` for instance members. `T` is a `PrimitiveValue` matched exactly, or `std::optional<Object>` for object-typed members (a `Get` returns a new owned handle; `Set` takes a `const Object*`, `nullptr` stores null).
- **Diagnosed, not undefined:** wrong static/instance form, a readonly field, a constant, a property with no public setter (all `InvalidArgument`, "read-only"), reading a write-only property ("write-only"), an object of the wrong type, a stale or foreign object.
- **Accessors:** properties run through their getter/setter with `DoNotWrapExceptions`, so a throwing accessor is `ManagedException` with its own type and details (never `TargetInvocationException`), and the object stays usable (tested).
- **NotFound:** a missing, private or differently typed (no widening) member; a field looked up as a property or the reverse; indexers.
- **Handles:** per-component, never reused, rejected once the component is released; the unload check looks up and uses a member before release and sees collection after.
- Object-typed members block `Component::Release` while a returned handle is held (tested).

Not done, by design: strings, structs and arrays as member types arrive with tickets 12–14.

Review fixes:
- An `init`-only property is public to reflection but is now read-only (`InvalidArgument`), since it is only valid while the object is constructed.
- Property lookup picks the most-derived declaration when a derived type hides a base property with another type (`GetProperty` would throw `AmbiguousMatchException`); a hidden field already resolved this way and is now tested.
- More coverage: `ulong`, `float` and `sbyte` members, and released and foreign objects passed to a member.
- Known gap, not fixed: inherited static members are found (members flatten the hierarchy) whereas static methods must be declared on the type. Both are documented as such; tell me if you want them unified.
