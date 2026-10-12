# 12: Parallel C# systems and threading

**What to build:** To be decided. v1 runs every script call on the main thread (spec, Threading). This ticket decides whether and how C# systems can run on the scheduler's thread pool:
- read-only/read-write access markers on queries, and conflict detection between systems;
- what Iris must guarantee for calls on different owners from different threads (the README's "designed but unverified" statement);
- the concurrency test suite review-18 T2 asks for;
- how deferred structural changes merge across parallel systems.

**Blocked by:** none

**Status:** needs-triage

## Comments

Filed from the ticket 19 grilling session (Q5). Likely needs its own grilling before any implementation tickets.
