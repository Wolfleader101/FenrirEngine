# 02: Fake-bridge checks for the remaining native paths

**What to build:** The native paths that no test reached, or that only the real bridge reached, are checked through the fake (spec: "Testing Decisions", seam 1).

Each is a plain Catch2 case with an `INFO` that says what went wrong and a literal expected value.

**Blocked by:** 01 (In-process bridge: test-only construction, the fake table and the snapshot checks).

**Status:** resolved

- [x] String blocks: a null block, a truncated block and bytes that are not valid UTF-8 fail the way the public documentation says, and the block is given back.
- [x] Struct registration refuses a handle id outside the range the native side can encode, and a handle of zero.
- [x] Array responses: a negative or huge length, an element call that fails, and a copy that reports the wrong count each fail cleanly.
- [x] A returned object handle of zero is an empty object only where the documentation says so, and an error elsewhere.
- [x] Every status the bridge can return, including one Iris does not know, maps as `CompleteCall` documents.
- [x] Diagnostics the bridge writes badly (a negative count, a count larger than the buffer, an invalid truncation flag) never cause an out-of-range read and are reported as documented.
- [x] What an owner does when a release is answered with `InvalidHandle` or `InUse` is pinned without a real component.
- [x] Every block handed out is given back in every case. The full CTest suite passes.

## Comments

Done. The cases are in `Tests/fake.cpp`, on the existing fake with no change to it: array create/length/get, struct registration (handle 0, 1<<22 and above refused, (1<<22)-1 accepted, a refused registration stops the array being created), zero handles from CreateObject, FindMethod and FindMember, a nullable object return of zero, string elements (plain, empty, embedded NUL, invalid UTF-8, null, rejected), every wire status including 2, 4, 99 and -1, every badly written diagnostic, and what a component or object owner does after a release answered with InvalidHandle or InUse.

One real defect found: a negative array length from the bridge was returned as a huge size. `RawArray::Length` now reports `InvalidResponse` ("GetArrayLength returned a negative length."); the case was red first.

The criteria that named things the native side cannot see were pinned as what it does. A string block carries no checksum, so a truncated block cannot be detected: Iris trusts the length prefix, and a block whose bytes are not valid UTF-8 is handed over as written (strict UTF-8 checks apply to diagnostics only). There is no count output on a copy, so "a copy that reports the wrong count" has no counterpart; a failed copy passes the status through like any other call. A mutation (struct handle bound widened) fails its case.

Review follow-ups: the zero-handle cases now script the zero instead of relying on the fake default; the object-release answers are literal pairs; a failed copy is pinned. Not done, on purpose: a length of INT64_MAX is a valid length (a size, not an error), so only the negative one is refused; a zero return from a non-optional `Object` method has no documented behaviour to pin, so it is left alone.
