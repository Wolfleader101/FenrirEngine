# 05: Export envelope: move the remaining exports and correct ADR-0002

**What to build:** Every other managed export (invoke, objects, members, structs, arrays, internal calls, type queries, wait for unload) moves onto the envelope and the hand-written copies go (spec: "The managed export envelope").

ADR-0002 says adding an export is "a new field on both sides plus a version bump". That understated the real cost, and after this change it needs to be stated as it then is.

**Blocked by:** 04 (Export envelope: introduce it and move the first exports).

**Status:** ready-for-agent

- [ ] Every export uses the envelope; no export repeats the diagnostic-tail validation, the name decode, the lookup-status mapping or the exception-to-status mapping by hand.
- [ ] The error-contract checks pass unchanged for every export. Any status that changed is recorded in this ticket as a deliberate fix, with the reason.
- [ ] ADR-0002's statement of what adding an export costs is amended to describe the steps after the change. The decision itself is unchanged.
- [ ] The bridge function table and `BridgeVersion` are unchanged. The full CTest suite passes.
