# 16: Type enumeration, relationships and attributes

**What to build:** Native code can list a component's types, query a type's base type and interfaces, test whether one type can be assigned to another, and read custom attributes and their constructor arguments. These are the building blocks for discovering scripts, without any engine-specific concepts.

**Blocked by:** 06

**Status:** resolved

- [x] Listing a component's public types returns type handles, and the order and filtering rules are documented.
- [x] Base type, interface list and an assignability check work across component and shared-contract assemblies.
- [x] Custom attributes (types only; methods/fields left for later) on a type (and on methods/fields where cheap) can be listed, with primitive and string constructor arguments readable.
- [x] Results are owning snapshots or validated handles, and none of them keep the component alive.
- [x] The fixture includes a small type hierarchy with attributes, used by the tests.

## Comments

### Resolution

- Native: `Component::GetTypes()`, `Type::GetBaseType/GetInterfaces/GetAttributes/IsAssignableTo` (by handle or by
  name). Bridge: `ListTypes`, `QueryType` (base / interfaces / attributes) and `IsTypeAssignable` (table slots 28-30,
  version 21). Snapshots are length-prefixed binary blocks (`TypeQueries`) copied once and given back with
  `FreeString`; the native `SnapshotReader` is bounds-checked, so a malformed block is `InvalidResponse`.
- Listing rules: public (`IsVisible`), non-generic-definition types of the component assembly, ordered by metadata
  token; same set as `FindType`. Interfaces sorted by full name for stability.
- Attributes are read through `CustomAttributeData` (no instantiation, no component code). Arrays are reported as
  null with their type name; a lone surrogate in a string argument becomes U+FFFD.
- Assignability by name resolves in the component assembly, then `Type.GetType` (framework), then each referenced
  assembly through the component's load context, so a contract resolves whether shared or private.
- Not done: attributes on methods, fields and properties (would extend `QueryType` with a member target); assembly
  level attributes; listing types of dependency assemblies.
- Fixture: `Hierarchy.cs` (Actor/Player/Enemy/Boss, `IScript` contract interface, `Script`/`Details` attributes,
  internal and generic types that must not be listed).
