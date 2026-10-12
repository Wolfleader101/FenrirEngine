# 25: Struct mappings for shared-assembly types, and managed field names

**What to build:** Found while building engine ticket 04 (`.scratch/engine-scripting/`). The engine's own components (Transform, Name, Relationship) are C# structs in the shared contract assembly `Fenrir.Scripting`, not in any script's assembly. Iris only looked up a mapped struct in the loaded component's own assembly, so the host could not validate them. Separately, a C# struct had to reuse the C++ member names exactly (`pos`, `rot`), which is a poor public API.

**Blocked by:** none

**Status:** resolved

- [x] A `StructMapping` whose `typeName` names a public struct in a shared assembly validates, and works as a method argument, return, field and property type, exactly like a component's own struct.
- [x] A struct in a component's private dependency (not shared) is still `NotFound`. (By construction: only the component's own assembly and the shared ones are searched. Not separately tested.)
- [x] `IRIS_FIELD_AS(Type, member, "ManagedName")` lists a field under the managed name; validation still checks name, offset and kind.
- [x] The README documents both.

## Comments

### Resolution

- **Lookup:** `TryRegisterStruct` searches the component's assembly, then a new `SharedAssemblies.FindType` over the assemblies shared with every component (public types only). The registered struct is still per component, so handles and generations behave as before.
- **Macro:** `IRIS_FIELD_AS(Type, member, name)` is `IRIS_FIELD` with an explicit managed name. Additive.
- **Compatibility (ADR-0006):** additive. A lookup that used to be `NotFound` can now succeed; nothing that worked changes.
- **Tests:** `components.cpp` maps `Iris.Examples.Contracts.SharedPoint` (a contract struct with fields `X` and `Renamed`) from a C++ struct with members `x` and `y` via `IRIS_FIELD_AS`, validates it, and round-trips it through a component method. Debug and Release.
- **Not tested here:** that an unshared contract's struct stays `NotFound`. Sharing is process-wide and permanent, and the component checks run in one process, so an earlier test has always shared the contract by then.
