# 04: Export envelope: introduce it and move the first exports

**What to build:** One managed module owns what every export does around its work (spec: "The managed export envelope"). This ticket introduces it and moves the load, release, force release, name-copy, unload-state, forget-unload, shared-assembly, log-sink and lookup exports onto it, so only their work remains in them.

The envelope validates the diagnostic tail and other outputs, sets outputs to their safe values, decodes names as strict UTF-8, runs the work, and turns the outcome or exception into a status and a diagnostic. A lookup result maps to a status once. The table's slots, signatures and version do not change.

**Blocked by:** 03 (Pin every managed export's error contract).

**Status:** resolved

- [x] The envelope module exists and the listed exports use it. Validation order is unchanged (diagnostic tail, other outputs, arguments, work).
- [x] The checks from the previous ticket pass unchanged for the moved exports.
- [x] Where the unified exception mapping would change a status a caller can get, the export keeps its mapping explicitly (as an option of the envelope with the reason in a comment) or the change is recorded in this ticket as a deliberate fix. No silent change.
- [x] The `FindResult`-to-status mapping is written once and used by every moved lookup.
- [x] The bridge function table, its layout and `BridgeVersion` are unchanged; the real-bridge and fixture-bridge checks pass.
- [x] The full CTest suite passes.

## Comments

Done. `ExportEnvelope.cs` holds a `DiagnosticTail` (built from the export's four diagnostic arguments: `IsValid` validates and clears them, `Fail` writes the diagnostic and returns the status, `Explain` writes a bridge-found explanation without reporting it), an `ExceptionMapping` option naming each export's mapping, `StatusOf(FindResult)` once for every lookup, and strict UTF-8 `Decode`/`DecodeName`. Moved: load, release, force release, name copy, unload state, forget unload, shared assembly, log sink, FindType, FindMethod, FindInstanceMethod and FindMember. The unmoved exports (wait for unload and everything else) are ticket 05. Each export still validates in the same order and keeps its own mapping, so no status changes.

One deliberate narrowing, recorded: LoadComponent used one mapping for decoding (undecodable text is InvalidArgument, else ManagedException) and another for the load (by kind). It now uses by-kind-and-decode for both, which differs only for a bridge argument or handle exception thrown while decoding, which strict UTF-8 decoding cannot throw. The checks from ticket 03 pass unchanged; a mutation to the shared lookup status (a stale handle answered NotFound) fails them. The table, its layout and `BridgeVersion` are unchanged.
