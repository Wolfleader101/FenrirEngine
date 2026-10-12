# 23: Waiting for an unload

**What to build:** Review-18 G4. An explicit, documented way for a host to confirm that a released component has been collected, such as `Host::CollectGarbage()` and/or `Component::WaitForUnload(timeout)` (decide which and record why). Iris's rule is that it never forces GC implicitly, and this keeps that rule: collection happens only when the host asks for it. Engine hot reload needs to confirm the old generation is gone before loading the new one, and to warn when a script leaks a root.

**Blocked by:** none

**Status:** ready-for-agent

- [ ] A released component with no roots is reported as `Collected` within the timeout.
- [ ] A component kept alive by a deliberate root (for example a leaked static or thread) times out and reports `Pending`, and the host stays usable.
- [ ] Calling it with nothing released, or on an already-collected component, is harmless.
- [ ] The README states the cost (a full blocking GC) and that it is only for reload points.
- [ ] Tests cover collected, leaked and timeout cases, in Debug and Release.

## Comments

Filed from the ticket 19 grilling session (engine-scripting spec, Q20). It blocks engine-scripting ticket 10 (hot reload).
