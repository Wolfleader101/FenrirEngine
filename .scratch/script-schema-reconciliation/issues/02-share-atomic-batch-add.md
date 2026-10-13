# 02: Share the atomic batch-add in the registry

**What to build:** One "add all or none" rule, with the duplicate-in-batch and already-taken checks, used by both the component table and the event table. Ids stay positions and the registry generation still changes on every mutation. Prefactor only; no behaviour change.

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] Component and event batch adds go through one shared rule
- [ ] Ticket 01 tests and all existing tests pass unchanged

Spec: `.scratch/script-schema-reconciliation/spec.md`
