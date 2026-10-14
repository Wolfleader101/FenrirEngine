# 02: Fake-bridge checks for the remaining native paths

**What to build:** The native paths that no test reached, or that only the real bridge reached, are checked through the fake (spec: "Testing Decisions", seam 1).

Each is a plain Catch2 case with an `INFO` that says what went wrong and a literal expected value.

**Blocked by:** 01 (In-process bridge: test-only construction, the fake table and the snapshot checks).

**Status:** ready-for-agent

- [ ] String blocks: a null block, a truncated block and bytes that are not valid UTF-8 fail the way the public documentation says, and the block is given back.
- [ ] Struct registration refuses a handle id outside the range the native side can encode, and a handle of zero.
- [ ] Array responses: a negative or huge length, an element call that fails, and a copy that reports the wrong count each fail cleanly.
- [ ] A returned object handle of zero is an empty object only where the documentation says so, and an error elsewhere.
- [ ] Every status the bridge can return, including one Iris does not know, maps as `CompleteCall` documents.
- [ ] Diagnostics the bridge writes badly (a negative count, a count larger than the buffer, an invalid truncation flag) never cause an out-of-range read and are reported as documented.
- [ ] What an owner does when a release is answered with `InvalidHandle` or `InUse` is pinned without a real component.
- [ ] Every block handed out is given back in every case. The full CTest suite passes.
