# 22: Member enumeration and member attributes

**What to build:** Review-18 G1. Additive snapshot queries on `Type` that list its fields, properties and methods, with each member's name, type (full name and value kind), offset for fields of blittable structs, static/instance and visibility, plus each member's attributes. They reuse ticket 16's snapshot and attribute machinery. The engine's editor inspector and component serialization need this to show and save C# component fields (`[Range(0, 1)]`, `[SerializeField]`-style attributes). Field-by-field migration of component layouts on reload also needs it.

**Blocked by:** none

**Status:** ready-for-agent

- [ ] Field, property and method listings return owning snapshots (no component references), in declaration order.
- [ ] Each member's attributes are returned with the same `AttributeInfo` shape as type attributes.
- [ ] Field offsets are reported for blittable structs and are consistent with struct-mapping validation.
- [ ] Inherited members are either included with their declaring type or excluded, and whichever it is is documented.
- [ ] Tests cover a struct, a class with properties and methods, attributes with arguments, and a released component.

## Comments

Filed from the ticket 19 grilling session (engine-scripting spec, Q20). It is not needed for the v1 runtime, but must land before the editor inspector or serialization work.
