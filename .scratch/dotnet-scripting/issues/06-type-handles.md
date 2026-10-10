# 06: Type handles

**What to build:** Native code looks up a type by its full name in a loaded component and receives an opaque owning `ManagedType` handle. The handle can report the type's name, and it becomes invalid when its component is released.

**Blocked by:** 02, 04

**Status:** ready-for-agent

- [ ] Looking up an existing public type returns a handle that reports the type's full name and assembly name.
- [ ] A missing type returns a not-found status with an owning diagnostic, not an exception.
- [ ] Handles are opaque, validated on every use, never reused, and bound to their component's generation.
- [ ] After the component is released, the type handle returns `InvalidHandle`. The handle and its managed cache do not keep the component alive (verified with the unload check).
- [ ] The example and tests exercise the lookup, the not-found case and the stale-handle case.
