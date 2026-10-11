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
  `Version.cs` differs observably (`Number`, `Name`, `Scale`, and `OnlyInA`/`OnlyInB`). CTest passes B's
  path to the managed, unload and component checks as `IRIS_TEST_COMPONENT_B`.
- Managed (`UnloadChecks.RunReload`): four A/B cycles. Each version is used through every API (types,
  methods, strings, structs, arrays, objects, members, internal calls, listing, attribute snapshots,
  assignability), released, and its context, assembly and private dependency are *observed collected* after the
  next version has loaded; every id from one version is dead against both its own token and the other's. A
  missing B assembly fails the load, leaves `TrackingCount` unchanged, and the next load works.
- Native (`components.cpp`): the same replacement through the public API, with stale `Type`/`Method`/`Member`/
  `Object`/`Array`/`Component` handles all `InvalidHandle`, plus a failed replacement followed by a good load.
- Retention: the existing retained-`Type` and leaked-object cases still show a reference *you* hold keeps
  unloading `Pending`; the reload cycles now also use bindings, caches (handle tables), attribute snapshots
  and type lists and still collect, which shows Iris holds no root of its own. Passes in Debug and Release.
