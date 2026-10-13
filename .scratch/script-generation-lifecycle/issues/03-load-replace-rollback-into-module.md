# 03: Move load, replace and rollback into the module

**What to build:** The reload sequence lives in the module, in the current order: play back recorded changes, stop the old systems, release and unload (waiting up to the timeout), load the new generation all-or-nothing, restore the previous copy on failure. Its collaborators (Iris host, the load-completion hook from the reconciliation module, the systems start/stop hook) are supplied, not created inside. The host state keeps only the logger and its mutex, the Iris host, the phase runner and context activation.

**Blocked by:** 02

**Status:** ready-for-agent

- [ ] Failures are returned as results across the module's interface
- [ ] `Reload()` behaves as before, including re-entrancy guard and exception handling
- [ ] Ticket 01 tests and all existing tests pass unchanged

Spec: `.scratch/script-generation-lifecycle/spec.md`
