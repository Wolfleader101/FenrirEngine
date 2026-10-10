# 04: Code cleanup from the review

**What to build:** Apply the review's cleanup findings so later API work builds on a lean, consistent base. Replace the repeated status/value/diagnostic/truncated result structs with one shared result shape. Cut the README down to a usage guide. Strip over-hedged and repetitive comments, leaving the essential lifetime and ownership contracts. Fix the correctness findings assigned to this ticket.

**Blocked by:** 01

**Status:** resolved

- [x] Every finding the review assigned to this ticket is fixed or explicitly deferred with a reason.
- [x] One shared result type (or template) replaces the duplicated result structs, and call sites read consistently.
- [x] The README is a concise usage guide: build, configure, run, and core lifetime rules. Design rationale moves to ADRs or the plan.
- [x] Comments state contracts once, at the declaration, and are not repeated at every call site.
- [x] There are no behaviour changes beyond the listed bug fixes. All CTests pass, and the Release unload checks still pass.

## Comments

From the ticket 03 review (review 01, O5): the fixture bridge still selects behaviour by magic numbers. `left` values 1–12 pick integer responses, 100–103 read counters, and 200+ select string or unload modes. Replace them with named scenarios as part of this cleanup.

**Done.** Review 01 findings assigned here:
- **O1, C4, D6:** `ManagedCallResult<T>` (plus `<void>`) replaces the five result structs, with one default status. `CompleteCall` is the single wire-status-to-result mapping. `LoadComponent` returns the owner by value.
- **O2:** `InitializationResult` is built by name, not by position.
- **O3:** the always-true `isCollectible` is removed, from the wire too (bridge table version 9).
- **O4:** `DiscoverHostFxr` is static.
- **C1:** a path that can't be converted to UTF-8 returns `InvalidArgument`. A Windows check covers it.
- **C2:** the `ManagedComponent` docs require destroying owners before `main` returns.
- **C3:** the bridge maps only its own validation (`BridgeArgumentException`, malformed UTF-8) to `InvalidArgument`. The `Overflow` mapping for component exceptions is deferred to ticket 07 (noted there).
- **O5:** the fixture bridge uses named scenarios, modes and counters.
- **T2:** each check program runs named cases, reports each one, and keeps going after a failure.
- **T3:** tests are renamed `ManagedContracts`, `ManagedUnload`, `Bridge-*` and `Components`; the projects are now `ManagedChecks` and `FixtureBridge`.
- **N1:** the README is now a roughly 200-line usage guide.
- **N2:** the public headers state each contract once.

9/9 CTests pass in Debug and Release on Windows.
