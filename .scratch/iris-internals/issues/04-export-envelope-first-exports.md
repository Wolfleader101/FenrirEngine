# 04: Export envelope: introduce it and move the first exports

**What to build:** One managed module owns what every export does around its work (spec: "The managed export envelope"). This ticket introduces it and moves the load, release, force release, name-copy, unload-state, forget-unload, shared-assembly, log-sink and lookup exports onto it, so only their work remains in them.

The envelope validates the diagnostic tail and other outputs, sets outputs to their safe values, decodes names as strict UTF-8, runs the work, and turns the outcome or exception into a status and a diagnostic. A lookup result maps to a status once. The table's slots, signatures and version do not change.

**Blocked by:** 03 (Pin every managed export's error contract).

**Status:** ready-for-agent

- [ ] The envelope module exists and the listed exports use it. Validation order is unchanged (diagnostic tail, other outputs, arguments, work).
- [ ] The checks from the previous ticket pass unchanged for the moved exports.
- [ ] Where the unified exception mapping would change a status a caller can get, the export keeps its mapping explicitly (as an option of the envelope with the reason in a comment) or the change is recorded in this ticket as a deliberate fix. No silent change.
- [ ] The `FindResult`-to-status mapping is written once and used by every moved lookup.
- [ ] The bridge function table, its layout and `BridgeVersion` are unchanged; the real-bridge and fixture-bridge checks pass.
- [ ] The full CTest suite passes.
