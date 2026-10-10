# 02: Bridge function table

**What to build:** Native initialization obtains every bridge entry point through a single managed entry point that fills a versioned table of unmanaged function pointers, instead of resolving each method by name. Adding a new export becomes: add a field to the table on both sides, then bump the table version once. There are no new readiness flags and no duplicated per-platform name constants.

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] One named managed entry point is resolved. It reports the table version and size, and fills the table.
- [ ] A version or size mismatch fails initialization with a diagnostic before any table entry is called.
- [ ] A table with a missing (null) entry fails initialization, and readiness stays false.
- [ ] The initialization result no longer carries a bool per method. Progress reporting stays meaningful (hostfxr loaded, runtime delegate obtained, bridge handshake).
- [ ] Native and managed table layouts are checked to agree (size/offset assertions on both sides, or an equivalent check).
- [ ] All existing CTests pass. The legacy/incomplete bridge fixtures are updated to exercise the version-mismatch and missing-entry failures.

## Comments

From review 01:
- **D1:** `load_assembly_and_get_function_pointer` loads the bridge into an *isolated* load context, one per assembly path. Decide where the bridge lives while doing this ticket (keep it isolated, or use `hdt_load_assembly` plus `hdt_get_function_pointer` to load into the default context), and record the decision as an ADR.
- **T1:** the managed checks currently run the bridge in the default context, so they don't match production. Add a native check for whichever placement you choose.
