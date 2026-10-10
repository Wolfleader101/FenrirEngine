# 06: Type handles

**What to build:** Native code looks up a type by its full name in a loaded component and receives an opaque owning `ManagedType` handle. The handle can report the type's name, and it becomes invalid when its component is released.

**Blocked by:** 02, 04

**Status:** resolved

- [x] Looking up an existing public type returns a handle that reports the type's full name and assembly name.
- [x] A missing type returns a not-found status with an owning diagnostic, not an exception.
- [x] Handles are opaque, validated on every use, never reused, and bound to their component's generation.
- [x] After the component is released, the type handle returns `InvalidHandle`. The handle and its managed cache do not keep the component alive (verified with the unload check).
- [x] The example and tests exercise the lookup, the not-found case and the stale-handle case.

## Comments

**Done** (10/10 CTests in Debug and Release on Windows).
- `Component::FindType(name)` returns an `Iris::Type`; `Type::GetInfo()` returns the full and assembly names. A missing, non-public or generic-definition type returns the new `NotFound` status (wire status 6) with a diagnostic naming the type. Bridge table version 11.
- **Deviation from the ticket wording:** the handle is a cheap, copyable, non-owning value rather than an owning one. The managed `Type` references live in a per-component table inside the active registry entry, so releasing the component drops them. A handle therefore needs no destructor, can outlive the host and the `Component` object, and cannot keep a component alive. Ids come from one process-wide counter and are never reused. Looking a type up twice returns the same id.
- Checked at the export level (`TypeChecks`), natively through the public API (three `Components` cases), in the unload check (a lookup before release, then collection), and in the example.

Left for later, from the review:
- `TryFindType` returns `NotFound` rather than `InvalidHandle` when the component is released while a missing type is being resolved.
- The `field` selector for type names is a bare int on both sides of the wire.
- The name validation is duplicated between `FindType` and `InvokeComponentInt32`. Ticket 07 replaces the latter.
- No `GLOSSARY.md` exists yet; ticket 18 creates it.
