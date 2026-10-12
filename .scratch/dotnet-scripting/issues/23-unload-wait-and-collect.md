# 23: Waiting for an unload

**What to build:** Review-18 G4. An explicit, documented way for a host to confirm that a released component has been collected, such as `Host::CollectGarbage()` and/or `Component::WaitForUnload(timeout)` (decide which and record why). Iris's rule is that it never forces GC implicitly, and this keeps that rule: collection happens only when the host asks for it. Engine hot reload needs to confirm the old generation is gone before loading the new one, and to warn when a script leaks a root.

**Blocked by:** none

**Status:** resolved

- [x] A released component with no roots is reported as `Collected` within the timeout.
- [x] A component kept alive by a deliberate root (for example a leaked static or thread) times out and reports `Pending`, and the host stays usable.
- [x] Calling it with nothing released, or on an already-collected component, is harmless.
- [x] The README states the cost (a full blocking GC) and that it is only for reload points.
- [x] Tests cover collected, leaked and timeout cases, in Debug and Release.

## Comments

### Resolution

- **Decision:** `Component::WaitForUnload(std::chrono::milliseconds)` only, no `Host::CollectGarbage()`. The engine asks "is this old generation gone?", and a bare collection cannot answer that; the wait does, and it is the only place a collection happens.
- **Behaviour:**
  - It returns `CallResult<ComponentUnloadState>`: `Collected`, or `Pending` when the timeout passes first.
  - While the component is `Pending`, each attempt runs a full blocking collection plus finalizers, then looks again about every 10 ms.
  - A loaded owner returns `NotRequested` and an already collected one returns `Collected`, both at once with no collection.
  - A negative timeout is `InvalidArgument`, and a moved-from owner is `InvalidHandle`.
  - It works after `Release`, like `GetUnloadState`.
- **Bridge:** one new function, `WaitForComponentUnload`, so the bridge version is **22** and the table has 31 slots. The bridge tests, the fixture bridge and the handshake check follow.
- **Tests:** two cases in `components.cpp`, run in Debug and Release.
  - The first covers a loaded owner, a negative timeout, a released component that is collected, a repeat call, and a moved-from owner.
  - The second leaks on purpose: a managed thread that runs until the host sets a native flag. The component stays `Pending` for the timeout, which is honoured; another component loads and runs meanwhile; and after the flag is set the same wait reports `Collected`.
- **Docs:** the README says the cost (a full blocking GC) and that it is for reload points only.


Filed from the ticket 19 grilling session (engine-scripting spec, Q20). It blocks engine-scripting ticket 10 (hot reload).
