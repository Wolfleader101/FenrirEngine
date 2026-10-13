# 04: Add the event adapter and delete the old event pipeline

**What to build:** Events go through the same module via a second adapter (clearing means removing the event queue from the App). The duplicated event discovery/check/diff code is deleted. The load path reads as "reconcile, then keep".

**Blocked by:** 03

**Status:** resolved

- [ ] No event discovery/check/diff logic left outside the module
- [ ] A third kind of script-defined thing would need only a new adapter
- [ ] Ticket 01 tests and all existing tests pass unchanged

Spec: `.scratch/script-schema-reconciliation/spec.md`
