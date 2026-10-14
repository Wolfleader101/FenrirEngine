# 01: In-process bridge: test-only construction, the fake table and the snapshot checks

**What to build:** An Iris maintainer can check how the native modules handle bridge responses with no .NET runtime (spec: "The in-process bridge for tests").

A test-only way builds a host, or directly a component, over a bridge function table the test supplies. It is declared outside the installed headers; the Iris test executable that uses it is given the library's private source directory. The fake fills every slot with a benign default, lets a case script what it needs, records calls, and allocates blocks the way the real bridge does and counts the ones given back. The first family of checks proves it: type snapshots.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] The test-only construction exists, is not in the public include directory or the installed headers, and adds nothing to the public interface beyond (at most) a friend declaration.
- [x] A new check executable runs in one process, starts no runtime, needs no environment variable, and is part of CTest.
- [x] The fake defaults every slot, scripts a slot's status, written values and diagnostic (including truncated), records calls and arguments, and counts blocks handed out against blocks given back. One fake is active at a time and is reset between cases.
- [x] Cases pin that each malformed type snapshot becomes `InvalidResponse` and is never read out of range: no block, a negative or oversized length, a truncated string, trailing bytes, an unknown type kind, an unknown visibility, an unknown attribute argument tag.
- [x] A case pins that every block the fake handed out is given back, including when parsing fails.
- [x] The existing real-bridge, fixture-bridge and components checks are untouched and pass. The full CTest suite passes.

## Comments

Done. The test-only construction is a header-only `Detail::TestAccess` in the library's private sources (`Native/src/TestAccess.hpp`, not installed), reached by a friend declaration on `Component` in the public header. That adds no API, only the declaration that a struct named `Detail::TestAccess` may build one. The fake (`Tests/Fake/FakeBridge.hpp`) has one `std::function` per slot with the slot's own signature, benign defaults (Success, nothing written; `FindType` answers a type; `freeString` gives back tracked blocks and counts anything else as a bad free), call counts by managed name, and `Block`, `BlockClaiming` and a `SnapshotWriter` for building snapshots. One fake is active at a time; a component made from it must be destroyed before it.

A mutation (the bad-length branch no longer gives its block back) fails its case. The check executable `IrisFakeChecks` does not wait for the managed build.

The fake counts calls by managed name; the arguments of a call are captured by the handler a case installs for that slot (it is a `std::function` with the slot's own signature), not recorded by the fake itself. `Calls` throws for a name the bridge does not have. A count check makes the slot list fail to compile if it misses a slot of the table.
