# 08: The active-entry step, and a registry left with lifecycle and handle tables

**What to build:** Resolving a token to its active entry, doing the work and publishing only if that entry is still active is written once (spec: "The managed registry split", active-entry step).

The operations that repeat it use the helper. After this ticket and the two before it, the registry holds the registration lifecycle (load, release, force release, unload tracking) and the five handle tables; method matching and arrays stay with lookup and the tables.

**Blocked by:** 07 (Managed struct mapping out of the registry).

**Status:** resolved

- [x] The helper exists and the operations that resolved the token and re-checked it by hand use it. Locking behaviour is unchanged.
- [x] The registry's remaining interface is the lifecycle and the handle tables, and no moved code is left behind.
- [x] The managed checks for release, force release, stale tokens, live objects and concurrent release pass unchanged.
- [x] Existing managed checks and the `Components` checks pass unchanged. The full CTest suite passes.

## Comments

Done. Two helpers, `TryActive(token, out entry)` and `IsActive(token, entry)` (callers hold the gate), replace the 25 hand-written copies (16 resolves, 9 re-checks) of resolving a token to its active entry and of re-checking, before publishing, that the entry is still the active one. Locking is unchanged: each site holds the gate where it did. What is left that reads the registrations directly is the lifecycle: the name, release, unload state and forget-unload. The registry now holds that lifecycle, the five handle tables and the lookups that fill them; the codec, the struct mapping and the envelope are out of it, and nothing moved was left behind.

One finding, not fixed here: no managed check reaches the "released while the work ran" re-check. Making `IsActive` always true passes the whole managed suite. A deterministic check needs component code that releases its own component mid-call, which means a new native hook field in the shared example component that every host loading it must bind (the C++ checks load the same component), so it is left as a follow-up rather than widening this ticket.
