# 08: The active-entry step, and a registry left with lifecycle and handle tables

**What to build:** Resolving a token to its active entry, doing the work and publishing only if that entry is still active is written once (spec: "The managed registry split", active-entry step).

The operations that repeat it use the helper. After this ticket and the two before it, the registry holds the registration lifecycle (load, release, force release, unload tracking) and the five handle tables; method matching and arrays stay with lookup and the tables.

**Blocked by:** 07 (Managed struct mapping out of the registry).

**Status:** ready-for-agent

- [ ] The helper exists and the operations that resolved the token and re-checked it by hand use it. Locking behaviour is unchanged.
- [ ] The registry's remaining interface is the lifecycle and the handle tables, and no moved code is left behind.
- [ ] The managed checks for release, force release, stale tokens, live objects and concurrent release pass unchanged.
- [ ] Existing managed checks and the `Components` checks pass unchanged. The full CTest suite passes.
