Status: ready-for-agent

# Deepen Iris's internals: an in-process bridge for tests, one export envelope, a split registry, one component-scoped call module

Origin: architecture review of Iris and FenrirScripting (2026-10-14), candidates 6 to 9. None of it changes the public interface frozen by ADR-0006 or the bridge function table's layout and version (ADR-0002). All of it is about where Iris's knowledge lives and how it is tested.

## Problem Statement

Iris works, and its public interface is frozen and well tested end to end. Its insides make changes slow and risky for the people who maintain it:

- **Native response handling is only reachable through a real runtime.** Every native module holds a pointer to the bridge function table, so the seam exists, and the managed `FixtureBridge` is a second adapter at it. But the fixture is a whole managed assembly with its own .NET build, needs one test process per mode, and scripts only names, unload states and integer invokes. Its other slots answer with nothing. So several native paths have no test or are reached only through the real bridge: a malformed type snapshot, a bad string block, a struct id out of range, an array response, a block that is not given back when parsing throws.
- **Every managed export repeats the same envelope.** Validating the diagnostic tail, decoding a name as strict UTF-8, turning an exception into a status and writing its diagnostic is written by hand in each of about thirty exports, around one to three lines of real work each. Adding one slot costs about twelve edits in seven files, not the "new field on both sides plus a version bump" ADR-0002 describes. The mapping from exception to status is not even the same in every export.
- **One managed class holds six concerns.** `ComponentRegistry` is twelve hundred lines holding the registration lifecycle, the handle tables, method matching, the value codec, struct description parsing and validation, and arrays. "How does a value cross?" means reading four regions of it plus the value kinds and internal calls. Resolving a token under the lock is written about thirty times. The managed checks cannot test the codec or struct validation without loading an Iris component, and eight of them keep their own copy of the value-kind numbers.
- **The native handles repeat each other.** Method, member and array handles each carry their own copies of "adopt a returned object", "take a returned string", "take a returned struct" and "is this object valid and from this component", and invoking a static method and an instance method are near copies. The duplicated plumbing forces helper declarations into the public headers' `Detail` section.

## Solution

From a maintainer's point of view:

- Iris's own tests can build a host or component over an in-process C++ bridge function table that they script, so any response the bridge could give (good, malformed, hostile) is checked in one fast process with no .NET.
- Adding a bridge export is writing its work; the envelope around it is written once.
- The managed bridge is split by concern, so the codec and the struct validation are modules with their own interface and tests, and the registry holds only the lifecycle and handle tables.
- The native handles share one module for the rules of a call that belongs to one component.

Nothing a user of Iris sees changes: not the public headers, the statuses, the diagnostics or the bridge version.

## User Stories

### In-process bridge for tests (candidate 6)

1. As an Iris maintainer, I want to build a host or component over a bridge function table my test supplies, so that native response handling needs no .NET runtime.
2. As an Iris maintainer, I want the fake table to answer every slot with a benign default, so that a test scripts only the slot it is about.
3. As an Iris maintainer, I want to script a slot's responses (a status, the values it writes, the diagnostic it writes and whether that was truncated), so that I can make the bridge say anything.
4. As an Iris maintainer, I want the fake to record each call and its arguments, so that I can check what native code sent.
5. As an Iris maintainer, I want to check that a malformed type snapshot becomes `InvalidResponse` and is never read out of range, for each way it can be malformed (no block, a negative or oversized length, a truncated string, trailing bytes, an unknown type kind, an unknown visibility, an unknown attribute argument tag).
6. As an Iris maintainer, I want to check that every block the fake hands out is given back, including when parsing fails or throws, so that a leak shows up as a failing test.
7. As an Iris maintainer, I want to check a returned string block that is null, truncated or not valid UTF-8, so that `TakeString` fails the way the public documentation says.
8. As an Iris maintainer, I want to check that struct registration refuses a handle id outside the range the native side can encode, and a handle of zero, so that a hostile bridge cannot make an invalid value kind.
9. As an Iris maintainer, I want to check array responses (a negative or huge length, an element call that fails, a copy that reports the wrong count), so that arrays fail cleanly.
10. As an Iris maintainer, I want to check that a returned object handle of zero is an empty object only where the documentation says so, and an error elsewhere.
11. As an Iris maintainer, I want to check every status the bridge can return, including one Iris does not know, so that `CompleteCall` is pinned for all of them.
12. As an Iris maintainer, I want to check diagnostics the bridge writes badly (a negative count, a count larger than the buffer, an invalid truncation flag), so that none becomes an out-of-range read.
13. As an Iris maintainer, I want to check what happens to an owner when the bridge answers `InvalidHandle` or `InUse` for a release, so that the owner contract is pinned without a real component.
14. As an Iris maintainer, I want the fake to be test-only and outside the installed headers, so that it cannot become part of the frozen interface.
15. As an Iris maintainer, I want the new checks to run in one process with no environment variables, so that they are quick to run and to debug.
16. As an Iris maintainer, I want the existing real-bridge and fixture-bridge checks to stay as they are, so that the end-to-end net is unchanged.

### One export envelope (candidate 7)

17. As an Iris maintainer, I want the work an export does to be the only thing I write when I add one, so that a slot costs its work and not twelve edits.
18. As an Iris maintainer, I want the diagnostic-tail validation written once, so that its rules cannot differ between exports.
19. As an Iris maintainer, I want the strict UTF-8 decode of a name written once, with an embedded NUL refused, so that every name argument is handled the same way.
20. As an Iris maintainer, I want the mapping from a `FindResult` to a status written once, so that `FindType`, `FindMethod`, `FindMember` and `RegisterStruct` cannot drift.
21. As an Iris maintainer, I want the mapping from an exception to a status written once, with an explicit and documented exception for any export that must differ, so that a difference is a decision and not an accident.
22. As an Iris maintainer, I want an export's outputs set to a safe value before its work runs and left that way on failure (a token zeroed, a required count zero, a state of minus one), as they are today.
23. As an Iris user, I want every status and diagnostic from every export unchanged, so that nothing I rely on moves.
24. As an Iris maintainer, I want the order of validation unchanged (the diagnostic tail first, then the other outputs, then the arguments, then the work), so that an export with several bad arguments answers as it does now.
25. As an Iris maintainer, I want the error contracts (invalid argument, invalid UTF-8, a diagnostic written, outputs reset) checked once through the envelope and once per export family, so that a regression names the rule it broke.
26. As an Iris maintainer, I want ADR-0002's cost of adding an export corrected, so that the document is true.

### A registry split by concern (candidate 8)

27. As an Iris maintainer, I want the value codec (slot to managed object and back, for every kind including struct, enum and array) in a module of its own, so that "how does a value cross?" has one place to read.
28. As an Iris maintainer, I want to test the codec over types and values without loading an Iris component, so that its edge cases (a stray non-zero bool byte, an out-of-range enum, a nested struct) are cheap to check.
29. As an Iris maintainer, I want struct description parsing and validation (the text format, the blittable rules, field order, offsets, nested types) together in a module of its own, so that the format and its checker change as one.
30. As an Iris maintainer, I want to test that validation over `Type`s and description text without a component, so that each mismatch message is pinned directly.
31. As an Iris maintainer, I want "resolve the token to its active entry, run, and publish only if it is still the active one" written once, so that the lock discipline cannot be forgotten in one of thirty places.
32. As an Iris maintainer, I want the registry left with the registration lifecycle and the handle tables, so that its remaining interface is what its name says.
33. As an Iris maintainer, I want the managed checks to share the bridge's own value-kind numbering instead of eight copies, so that renumbering a kind cannot pass in some files and fail in others.
34. As an Iris maintainer, I want one check that the native and managed numbering agree (value kinds, snapshot kinds, visibilities, statuses), so that a mismatch fails loudly in one place.
35. As an Iris user, I want every behaviour of the bridge unchanged, so that components and hosts built against it keep working.

### Component-scoped calls on the native side (candidate 9)

36. As an Iris maintainer, I want the rules of a call that belongs to one component (validate object arguments, adopt a returned object, take a returned string or struct) written once, so that each ownership rule is fixed and tested in one place.
37. As an Iris maintainer, I want method, member and array handles to delegate to that module, so that they stop carrying three copies.
38. As an Iris maintainer, I want invoking a static method and an instance method to share their body, so that the free-the-block-on-throw handling cannot differ.
39. As an Iris maintainer, I want the object-belongs-to-this-component check written once, so that the four places that need it cannot disagree.
40. As an Iris maintainer, I want the public headers' `Detail` section to lose the per-handle adopt and take declarations that only existed to be duck-typed, so that less implementation detail sits where users can see it.
41. As an Iris user, I want the ownership rules (owners, handles, release) exactly as documented, so that nothing about lifetimes moves.
42. As an Iris maintainer, I want the in-process bridge to check this module's rules, so that the refactor has a net that does not need a runtime.

## Implementation Decisions

### The in-process bridge for tests

- A test-only way to make a host, or directly a component, over a caller-supplied bridge function table. The tests (not the public interface) use it. Production code has no new route to it.
- It is declared in a header that is not installed and not in the public include directory. The Iris test executable that uses it is given the library's private source directory on its include path. A friend declaration on `Host` or `Component` is acceptable if the state needs it, and is not an API change (ADR-0006).
- The bridge function table is a set of plain function pointers with no context, so the fake is process-wide state: one fake is active at a time, reset between cases, as the managed `FixtureBridge` already is. Cases run one after another.
- The fake fills every slot with a benign default (success, nothing written, no blocks) and lets a case replace what it needs. It records calls. It allocates blocks the way the real bridge does (so a block handed out is something Iris must give back) and counts the ones returned, which makes a leak a failed check.
- The table's layout, its version and the handshake are not part of this seam: the handshake is checked against the real and the fixture bridge, as today.
- The fake lives in the tests. It is not a mock of Iris's own modules: it stands where the managed bridge stands.

### The managed export envelope

- One module owns what every export does around its work: validate the diagnostic tail and the other output pointers, set outputs to their safe values, decode string arguments (strict UTF-8; empty, null, invalid and embedded-NUL names refused as `InvalidArgument`), run the work, and turn its outcome into a status and a diagnostic. The work of an export is then the only code in it.
- Exceptions map to statuses once: an argument error and a UTF-8 decode failure are `InvalidArgument`, a stale handle is `InvalidHandle`, anything else is `ManagedException`. **The exports do not all map the same way today** (some map a decode failure and an argument error to `InvalidArgument` and everything else to `ManagedException`, without an `InvalidHandle` case). Before unifying, each export's reachable mappings are pinned by a check. Where the unified mapping would change a status a caller can get, the export keeps its mapping explicitly (as an option of the envelope, with the reason in a comment) or the change is recorded as a deliberate fix in the ticket. No silent change.
- A `FindResult` maps to a status once, shared by every export that looks something up.
- The envelope is internal to the managed bridge. The table's slots, their signatures and `BridgeVersion` do not change. Putting the diagnostic tail into one argument would change the table and is out of scope.
- ADR-0002's sentence about the cost of adding an export is corrected in the same change as the code that makes it true.

### The managed registry split

- **Value codec:** the conversions between the wire slot and a managed object, for every kind, with the struct, enum and array helpers they need. Its interface is a type and a value in, a value out.
- **Struct mapping:** the description text format, its parsing, the checks of the description against the managed type (blittable rules, sizes, offsets, field order, nested types), and the record it produces.
- **Active-entry step:** one helper for "resolve the token, do the work on the entry, and republish only if the entry is still active", used by the operations that need it.
- **Registry:** the registration lifecycle (load, release, force release, unload tracking) and the five handle tables.
- Method matching stays with method lookup, arrays with the handle tables, until a clearer split shows itself. This spec does not require it to go further.
- The managed checks reference the bridge's own value-kind definitions in place of their copies. A shared table of the numbering (value kinds, snapshot type kinds, visibilities, statuses) in the tests is read by both a managed check and a native check, so each side is pinned to the same literal numbers. The check fails if either side renumbers.
- Nothing about the value kinds on the wire changes.

### The native component-scoped call module

- One source-private module holds the rules for a call that belongs to one component's bridge: that an object argument is valid and belongs to this component; adopting a returned object handle into an owner; taking a returned string block; taking a returned struct block; freeing a block however the call ends.
- The method, member and array handles call it instead of carrying their own. Invoking a static and an instance method share a body.
- The public templates (`Method`, `InstanceMethod`, `Member`, `Array`) keep their interface and behaviour. The part of `Detail` they need stays; the per-handle declarations that existed only so one template could call three handle types are removed from the public headers. `Detail` is not API (ADR-0006), so this is allowed.
- No ownership or lifetime rule changes: owners, handles, tokens, release and forced release are as `GLOSSARY.md` and ADR-0005 say.

## Testing Decisions

- **A good test here** crosses one of three seams and asserts what a caller can observe: a status, a value, a diagnostic, a log message, whether a block was given back. It names no private member and does not check the order of internal calls unless that order is the contract. Expected values are literals.
- **Seams:**
  1. The native bridge function table, for the native modules, through the new in-process fake. Tests are Catch2 cases in a new check executable, with the repository's test conventions (one behaviour per case, an `INFO` that says what went wrong).
  2. The managed bridge's exports and its modules, through the managed checks that already call them in process. The codec and struct validation get direct cases, with no component loaded.
  3. Iris's public interface against the real bridge, which is the existing `Components` checks and the consumer package, unchanged. They are the end-to-end net for every refactor in this spec.
- **Characterise before moving.** For #7, #8 and #9 each move starts with a check of the behaviour about to move, written against the old code and passing, then the move, then the check unchanged and still passing. Where the old code is unreachable by a check (the mapping differences between exports), the check is written for the difference.
- **Order.** #6 goes first: it is the net for the native work. #7, #8 and #9 are independent of each other; #9 is the one that gains most from #6.
- **Prior art:** the managed checks' per-area files (strings, structs, arrays, members, internal calls); the existing `FixtureBridge` and its `[fixture]` cases, which stay untouched; `Components` for the end-to-end behaviour; FenrirECSTests for the style of a check executable with no runtime.

## Out of Scope

- Replacing or shrinking the managed `FixtureBridge` and its six test processes. Its handshake-rejection cases need a real managed bridge entry point, and migrating its scripted-response cases onto the in-process fake is a possible follow-up once the fake exists.
- Changing the bridge function table: its layout, its version, or putting the diagnostic tail into one argument.
- Any change to the public headers beyond a friend declaration, or to a status, diagnostic or documented rule.
- Moving the native wire codec (the value-kind encoding that lives inside the method header's `Detail`) into a module of its own. It is related, and a later change can do it once the component-scoped module exists.
- String results, structs by value, or any other new marshalling.
- Making `Iris` generic over other runtimes or hosts.

## Further Notes

- Where the review called the unchecked mirrors "unavoidable", the spec keeps them but checks them: two sides that must agree are tested against one literal list.
- The count that matters for #7 is mostly mechanical, so the characterisation checks should be driven from a table of exports and their error cases rather than written one export at a time.
- ADR-0006's review rule applies: a pull request that touches `include/Iris` is reviewed as additive or breaking. Here it should be neither; if one is needed, it is a sign the change has gone beyond this spec.
