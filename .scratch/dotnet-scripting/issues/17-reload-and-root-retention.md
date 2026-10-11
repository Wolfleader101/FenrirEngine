# 17: Reload and root-retention checks

**What to build:** Two fixture versions with observably different behaviour show that component code can be replaced. Load version A, use every API feature (types, methods, objects, fields, strings, structs, arrays, internal calls, attributes), release everything, observe the unload, then load version B in a fresh context and observe the new behaviour. No handle from A ever works again.

**Blocked by:** 10, 11, 14, 15, 16

**Status:** resolved

- [x] Version B's behaviour is observed after A has been unloaded.
- [x] Every component, type, method, member, object and array handle from A returns `InvalidHandle` after the reload, and never selects anything from B.
- [x] Repeated A/B cycles complete, and the unload is observed each time.
- [x] Targeted retention checks: caches, internal-call bindings, attribute snapshots and a deliberately leaked object each keep the unload Pending until cleared.
- [x] Failing partway through a reload (for example a missing B assembly) leaves no registry roots, and the host stays usable.
- [x] The checks also run in Release to exercise optimized JIT lifetimes.

## Comments

From review 01 (D5): components load with `LoadFromAssemblyPath`, which locks the file on Windows. Separate fixture paths for versions A and B avoid this. Replacing a component in place would need stream or shadow-copy loading, which is out of scope for now.

### Resolution

- Fixture: the example component builds twice. Version B is the same project published with
  `-p:IrisFixtureVersion=B` (defines `FIXTURE_B`) into `component-b/` with its own intermediate directory;
  `ComponentVersion.cs` differs observably (`Number`, `Name`, `Scale`, and `OnlyInA`/`OnlyInB`). CTest passes B's
  path to the unload and component checks as `IRIS_TEST_COMPONENT_B`.
- Managed (`UnloadChecks.RunReload`): four A/B cycles. Each version is used through every API (types,
  methods, strings, structs, arrays, objects, members, internal calls, listing, attribute snapshots,
  assignability). Even cycles load B only after A is *observed collected*; odd cycles load B while A is merely
  released. A's ids are checked against B's **live** token (and B's against A's), and B's ids are asserted newer
  than every one of A's, so an old id cannot alias a new one. Context, assembly and private dependency of both
  versions are observed collected.
- Failing partway: with the internal calls unbound the load fails *after* the context and assembly exist; the
  registry counts are unchanged and the half-loaded assembly is observed to leave memory. A missing file also
  fails cleanly, and the next version loads.
- Native (`components.cpp`): the same replacement through the public API (stale `Type`/`Method`/`Member`/
  `Object`/`Array`/`Component` handles are `InvalidHandle`, B keeps working) and a failed replacement followed by
  a good load. The native tests do not wait for collection: nothing in the public API forces a GC, so the
  managed checks own that observation.
- Retention, honestly: the *positive controls* are a host-held managed object and a host-held array, which keep
  the released component `Pending` until dropped, then it collects (plus the existing retained-`Type` case).
  For bindings, handle tables, type lists and attribute snapshots the evidence is non-retention only: they are
  plain ids/bytes, components that used all of them were observed to collect, and there is no seam to pin them.
  A leaked object under a *forced* release is dropped and collects (existing case).
- Release: both configurations are run by hand (`ctest -C Release`); CMake does not force it, and CI is out of scope.
