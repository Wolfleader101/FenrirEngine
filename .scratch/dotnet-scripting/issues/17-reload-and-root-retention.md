# 17: Reload and root-retention checks

**What to build:** Two fixture versions with observably different behaviour show that component code can be replaced. Load version A, use every API feature (types, methods, objects, fields, strings, structs, arrays, internal calls, attributes), release everything, observe the unload, then load version B in a fresh context and observe the new behaviour. No handle from A ever works again.

**Blocked by:** 10, 11, 14, 15, 16

**Status:** ready-for-agent

- [ ] Version B's behaviour is observed after A has been unloaded.
- [ ] Every component, type, method, member, object and array handle from A returns `InvalidHandle` after the reload, and never selects anything from B.
- [ ] Repeated A/B cycles complete, and the unload is observed each time.
- [ ] Targeted retention checks: caches, internal-call bindings, attribute snapshots and a deliberately leaked object each keep the unload Pending until cleared.
- [ ] Failing partway through a reload (for example a missing B assembly) leaves no registry roots, and the host stays usable.
- [ ] The checks also run in Release to exercise optimized JIT lifetimes.

## Comments

From review 01 (D5): components load with `LoadFromAssemblyPath`, which locks the file on Windows. Separate fixture paths for versions A and B avoid this. Replacing a component in place would need stream or shadow-copy loading, which is out of scope for now.
