# 16: Type enumeration, relationships and attributes

**What to build:** Native code can list a component's types, query a type's base type and interfaces, test whether one type can be assigned to another, and read custom attributes and their constructor arguments. These are the building blocks for discovering scripts, without any engine-specific concepts.

**Blocked by:** 06

**Status:** ready-for-agent

- [ ] Listing a component's public types returns type handles, and the order and filtering rules are documented.
- [ ] Base type, interface list and an assignability check work across component and shared-contract assemblies.
- [ ] Custom attributes on a type (and on methods/fields where cheap) can be listed, with primitive and string constructor arguments readable.
- [ ] Results are owning snapshots or validated handles, and none of them keep the component alive.
- [ ] The fixture includes a small type hierarchy with attributes, used by the tests.
