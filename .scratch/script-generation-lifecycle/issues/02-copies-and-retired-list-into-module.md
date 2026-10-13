# 02: Move the copies and the retired list into a generation lifecycle module

**What to build:** A generation lifecycle module owns the scratch directory, per-generation copies (active, previous, retired), the retired list and its sweep. `Generation()` and `PendingUnloadCount()` read from it. Config values are passed at construction.

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] Host state no longer holds copy-directory or retired-list fields
- [ ] Ticket 01 tests and all existing tests pass unchanged

Spec: `.scratch/script-generation-lifecycle/spec.md`
