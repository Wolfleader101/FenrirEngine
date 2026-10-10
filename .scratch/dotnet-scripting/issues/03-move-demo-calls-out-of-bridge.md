# 03: Move demo calls out of the production bridge

**What to build:** The production bridge and the public host API keep only host lifetime and component operations. The demo calls (`AddInt32`, `GetGreeting`, `EchoUtf8`) leave the bridge. The behaviour they verified (the per-call diagnostic contract, truncation that preserves UTF-8, overflow recovery) is re-verified through the example component and test fixtures instead.

**Blocked by:** 02

**Status:** ready-for-agent

- [ ] The public host API no longer exposes the demo calls, and the bridge function table no longer contains them.
- [ ] The diagnostic contract is still covered by tests: an exception type is identified, truncation preserves valid UTF-8, success carries no stale text, and later calls recover. Coverage now goes through component invocation or a fixture.
- [ ] Coverage of strict UTF-8 handling is kept until ticket 12 supersedes it. A fixture-only path is acceptable.
- [ ] The hosting example demonstrates host start-up plus a component load and call, without the demo calls.
- [ ] All CTests pass.
