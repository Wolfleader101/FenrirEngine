# 22: Member enumeration and member attributes

**What to build:** Review-18 G1. Additive snapshot queries on `Type` that list its fields, properties and methods, with each member's name, type (full name and value kind), offset for fields of blittable structs, static/instance and visibility, plus each member's attributes. They reuse ticket 16's snapshot and attribute machinery. The engine's editor inspector and component serialization need this to show and save C# component fields (`[Range(0, 1)]`, `[SerializeField]`-style attributes). Field-by-field migration of component layouts on reload also needs it.

**Blocked by:** none

**Status:** resolved

- [x] Field, property and method listings return owning snapshots (no component references), in declaration order.
- [x] Each member's attributes are returned with the same `AttributeInfo` shape as type attributes.
- [x] Field offsets are reported for blittable structs and are consistent with struct-mapping validation.
- [x] Inherited members are either included with their declaring type or excluded, and whichever it is is documented.
- [x] Tests cover a struct, a class with properties and methods, attributes with arguments, and a released component.

## Comments

### Resolution

- **API:** `Type::GetFields()`, `GetProperties()` and `GetMethods()` return owning snapshots (`FieldInfo`, `PropertyInfo`, `MethodInfo`, `ParameterInfo`) with a public `TypeKind` and `MemberVisibility`. They ride on ticket 16's `QueryType` export (queries 3, 4 and 5), so there is no new bridge function.
- **Inherited members: excluded.** Only members declared on the type are listed. Inherited ones belong to the base type, reachable through `GetBaseType`, and listing them would bury a component's own fields under `System.Object`'s. This is documented in the README and the header.
- **Also excluded:** compiler-generated members (backing fields), property and event accessors, constructors, operators, and indexers. Non-public members are listed with their visibility, because an editor needs a private field that is marked for serialization.
- **Attributes:** the same `AttributeInfo` shape as a type's, read from metadata. Compiler-emitted attributes (such as `NullableContext`) are real metadata and are listed.
- **Offsets:** an instance field of a blittable struct reports its byte offset from the same runtime layout struct-mapping validation uses; the test compares it with `offsetof` on a validated mapping.
- **Tests:** one case in `components.cpp` covers a struct, a class with a base class, properties and methods, attributes with positional and named arguments, offsets, visibility, readonly and const, the excluded members, and a released component (plus a snapshot that survives release).


Filed from the ticket 19 grilling session (engine-scripting spec, Q20). It is not needed for the v1 runtime, but must land before the editor inspector or serialization work.
