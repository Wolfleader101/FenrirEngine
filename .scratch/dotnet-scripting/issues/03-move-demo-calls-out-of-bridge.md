# 03: Move demo calls out of the production bridge

**What to build:** The production bridge and the public host API keep only host lifetime and component operations. The demo calls (`AddInt32`, `GetGreeting`, `EchoUtf8`) leave the bridge. The behaviour they verified (the per-call diagnostic contract, truncation that preserves UTF-8, overflow recovery) is re-verified through the example component and test fixtures instead.

**Blocked by:** 02

**Status:** resolved

- [x] The public host API no longer exposes the demo calls, and the bridge function table no longer contains them.
- [x] The diagnostic contract is still covered by tests: an exception type is identified, truncation preserves valid UTF-8, success carries no stale text, and later calls recover. Coverage now goes through component invocation or a fixture.
- [x] Coverage of strict UTF-8 handling is kept until ticket 12 supersedes it. A fixture-only path is acceptable.
- [x] The hosting example demonstrates host start-up plus a component load and call, without the demo calls.
- [x] All CTests pass.

## Comments

**Done.**
- `AddInt32`, `GetGreeting` and `EchoUtf8` are gone from the public host API, the native table and the managed bridge. The table is now version 8 with six component slots.
- The fixture's synthetic component now carries the diagnostic contract checks:
  - Integer cases (truncation, broken formatter, invalid metadata, success with metadata, unknown status, embedded null, call counting) run through `ManagedComponent::InvokeInt32`.
  - UTF-8 adapter cases (query/copy failures, no retries, embedded null) run through `ManagedComponent::GetInfo`.
- Real-bridge exception details, Unicode and recovery stay covered by `components.cpp`.
- Strict UTF-8 decoding is still covered on the real bridge by malformed type names and Unicode component paths; ticket 12 supersedes this.
- The example is now about 100 lines: start, load, call, failure, release, stale call.
- 9/9 CTests pass in Debug and Release.
