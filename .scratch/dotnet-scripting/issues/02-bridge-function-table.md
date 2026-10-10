# 02: Bridge function table

**What to build:** Native initialization obtains every bridge entry point through a single managed entry point that fills a versioned table of unmanaged function pointers, instead of resolving each method by name. Adding a new export becomes: add a field to the table on both sides, then bump the table version once. There are no new readiness flags and no duplicated per-platform name constants.

**Blocked by:** 01

**Status:** resolved

- [x] One named managed entry point is resolved. It reports the table version and size, and fills the table.
- [x] A version or size mismatch fails initialization with a diagnostic before any table entry is called.
- [x] A table with a missing (null) entry fails initialization, and readiness stays false.
- [x] The initialization result no longer carries a bool per method. Progress reporting stays meaningful (hostfxr loaded, runtime delegate obtained, bridge handshake).
- [x] Native and managed table layouts are checked to agree (size/offset assertions on both sides, or an equivalent check).
- [x] All existing CTests pass. The legacy/incomplete bridge fixtures are updated to exercise the version-mismatch and missing-entry failures.

## Comments

From review 01:
- **D1:** `load_assembly_and_get_function_pointer` loads the bridge into an *isolated* load context, one per assembly path. Decide where the bridge lives while doing this ticket (keep it isolated, or use `hdt_load_assembly` plus `hdt_get_function_pointer` to load into the default context), and record the decision as an ADR.
- **T1:** the managed checks currently run the bridge in the default context, so they don't match production. Add a native check for whichever placement you choose.

**Done.**
- The bridge now loads into the default load context (`hdt_load_assembly`). Its one public entry point, `GetBridgeFunctions`, fills a version-7 table of nine function pointers. The decision is recorded in `docs/adr/0002-bridge-in-default-load-context-with-function-table.md`.
- Tests:
  - One fixture build covers four failure modes, each running as its own CTest process: legacy version, oversized table, missing entry, and a non-bridge assembly.
  - A managed check confirms that a mismatched handshake leaves the table untouched.
  - The in-process incomplete-bridge reset check was replaced by a failed initialization, since one process can now hold only one bridge.
- 9/9 CTests pass in Debug and in Release on Windows.
