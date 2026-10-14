# 01: In-process bridge: test-only construction, the fake table and the snapshot checks

**What to build:** An Iris maintainer can check how the native modules handle bridge responses with no .NET runtime (spec: "The in-process bridge for tests").

A test-only way builds a host, or directly a component, over a bridge function table the test supplies. It is declared outside the installed headers; the Iris test executable that uses it is given the library's private source directory. The fake fills every slot with a benign default, lets a case script what it needs, records calls, and allocates blocks the way the real bridge does and counts the ones given back. The first family of checks proves it: type snapshots.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] The test-only construction exists, is not in the public include directory or the installed headers, and adds nothing to the public interface beyond (at most) a friend declaration.
- [ ] A new check executable runs in one process, starts no runtime, needs no environment variable, and is part of CTest.
- [ ] The fake defaults every slot, scripts a slot's status, written values and diagnostic (including truncated), records calls and arguments, and counts blocks handed out against blocks given back. One fake is active at a time and is reset between cases.
- [ ] Cases pin that each malformed type snapshot becomes `InvalidResponse` and is never read out of range: no block, a negative or oversized length, a truncated string, trailing bytes, an unknown type kind, an unknown visibility, an unknown attribute argument tag.
- [ ] A case pins that every block the fake handed out is given back, including when parsing fails.
- [ ] The existing real-bridge, fixture-bridge and components checks are untouched and pass. The full CTest suite passes.
