# 04: Move the watcher decision into the module

**What to build:** The decision "nothing to do / still settling / ready to reload" (stable stamp, debounce, file openable) is computed inside the module from the current time as an input. The host state loses its stamp, debounce and poll fields. The pre-update system just asks the module.

**Blocked by:** 03

**Status:** ready-for-agent

- [ ] Host state holds no file-stamp, debounce or poll-time fields
- [ ] Frame cost when nothing changes is not worse
- [ ] Ticket 01 tests and all existing tests pass unchanged

Spec: `.scratch/script-generation-lifecycle/spec.md`
