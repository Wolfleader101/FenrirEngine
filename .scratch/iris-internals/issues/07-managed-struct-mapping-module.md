# 07: Managed struct mapping out of the registry

**What to build:** The description format for mapped structs and the code that checks it change as one module (spec: "The managed registry split", struct mapping).

Description parsing and the checks of the description against the managed type (blittable rules, sizes, offsets, field order, nested types) move out of the registry, together with the record they produce.

**Blocked by:** 06 (Managed value codec out of the registry).

**Status:** resolved

- [x] The struct mapping is its own module and the registry calls it. Behaviour is unchanged.
- [x] Direct managed checks, with no component loaded, pin each mismatch message: a wrong size, a wrong offset, a wrong field name or order, a wrong field kind, a nested type not registered first, a non-blittable type, an inline array of the wrong length or element.
- [x] Registering the same type again still validates again and returns the same handle.
- [x] Existing managed checks and the `Components` checks pass unchanged. The full CTest suite passes.

## Comments

Done. `StructMapping.cs` owns the description format (`Parse` into a `Description`) and its check against the managed type (`Describe`, with the blittable rules, sizes, offsets, field order, nested types and inline arrays), and produces the `StructRecord`. The registry keeps finding the type in the component or the shared assemblies and the handle table; the nested types registered so far reach the module through a small `IRegisteredStructs`, which the registry implements over its handle table. `PrimitiveKindOf` and `BlittableProblem`, which the internal-call and type-query code also used, moved with it. Order is unchanged: the header is read before the type is looked up, and an enum's underlying part is read after.

The new group "struct mapping" runs with no component and pins the explanation of each mismatch as a literal: wrong size, missing field, wrong name, wrong offset, wrong kind, nested type not registered first, a string field, a class, an inline array of the wrong length and of the wrong element, and the enum cases and malformed descriptions. A mutation to the offset message fails it. That a type registered again is validated again and keeps its handle was already pinned by the structs group (mismatches are tried after the struct is registered); that stays as it is.
