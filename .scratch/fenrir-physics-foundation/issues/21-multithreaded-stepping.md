# 21: Multi-threaded stepping on the engine's thread pool

**What to build:** Physics steps using the engine's worker threads without changing results (spec: "FenrirPhysics: stepping"; ADR-0010's contained threading). FenrirScheduler's `ThreadPool` exposes its thread count; a private `JobSystem` adapter runs Jolt's jobs on it. Physics's own systems stay sequential on the thread that runs the App, and the adapter must not deadlock when the waiting thread's jobs are queued to the same pool. `PhysicsConfig` can still choose single-threaded stepping. The spike stepped single-threaded only, so this is the main unproven risk: build it early.

**Blocked by:** 12 (Physics tracer bullet).

**Status:** resolved

- [ ] `ThreadPool` reports its thread count.
- [ ] A scene of many bodies steps multi-threaded with results bit-identical to single-threaded stepping (test).
- [ ] No deadlock with pool sizes of 1, 2 and the default, including while other parallel systems run in the same frame (test).
- [ ] The DemoApp physics scene runs multi-threaded (checked by hand).

**Resolved by:** merge of impl/21-multithreaded-stepping (88ca135)
**Hand checked (2026-10-16):** the DemoApp physics scene runs on worker threads (default config) without stalls.
