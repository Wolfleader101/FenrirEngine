# 05: Export envelope: move the remaining exports and correct ADR-0002

**What to build:** Every other managed export (invoke, objects, members, structs, arrays, internal calls, type queries, wait for unload) moves onto the envelope and the hand-written copies go (spec: "The managed export envelope").

ADR-0002 says adding an export is "a new field on both sides plus a version bump". That understated the real cost, and after this change it needs to be stated as it then is.

**Blocked by:** 04 (Export envelope: introduce it and move the first exports).

**Status:** resolved

- [x] Every export uses the envelope; no export repeats the diagnostic-tail validation, the name decode, the lookup-status mapping or the exception-to-status mapping by hand.
- [x] The error-contract checks pass unchanged for every export. Any status that changed is recorded in this ticket as a deliberate fix, with the reason.
- [x] ADR-0002's statement of what adding an export costs is amended to describe the steps after the change. The decision itself is unchanged.
- [x] The bridge function table and `BridgeVersion` are unchanged. The full CTest suite passes.

## Comments

Done. All 30 status-returning exports now build a `DiagnosticTail`, name their `ExceptionMapping`, decode names through `ExportEnvelope` and report failures through the tail; the array exports' shared helper and the old `StatusFor` are gone. `RegisterStruct` uses the one `FindResult` mapping. `CreateObject` and `IsTypeAssignable` keep a small result switch, because their results are `CreateResult` and `AssignResult` (three-way results with a different meaning), but explain NotFound through the tail. No status changed; the ticket 03 checks pass unchanged and the table, its layout and `BridgeVersion` are untouched. ADR-0002 now lists the steps for adding an export as they are after this change; the decision is unchanged.

Clarified after review: the 30 exports build 28 tails because release and force release share one core and the two method lookups share one. What each export still writes by hand is the tail construction, its try/catch and the output reset; those cannot move into a shared call without a closure per call, which the invoke and array paths should not pay.
