Status: ready-for-agent

# Test scripting's rules without starting .NET

Origin: architecture review of Iris and FenrirScripting (2026-10-14), candidate 5. Follows the same move as `.scratch/fenrirecs-tests`, which gave the ECS its own tests that need no .NET.

## Problem Statement

FenrirScripting has rules that are plain C++: what happens to a script's recorded changes when they play back, what is refused when a script assembly reports what it defines, which script data survives a reload, and when the file watcher decides a build is finished. The rules are small and exact, and each has a failure that matters (an entity reused by mistake, stale bytes read as a new layout, a half-registered load, a reload of a half-written file).

A maintainer can only test any of them by starting .NET: building the fixture script assemblies, loading one through Iris, and driving the rule from C#. For the refusal rules this needs a second, hand-written C# assembly that lies about what it defines, switched by an environment variable. So:
- a failing rule has to be reproduced through C#, and the failure may be in the C# or the C++;
- the suite takes ten seconds and needs every fixture built before one rule can be checked;
- the public `Scripting` interface has operations (`ApplyChanges`, `DiscardChanges`) that nothing in the engine calls: only the tests do, to stand in for the system runner.

## Solution

From a maintainer's point of view, FenrirScripting has a second test executable that never starts .NET and loads no fixture. It checks the change buffer, the reconcile rules and the reload watcher's decision through three package-internal seams, each with plain data in and plain data out. It runs in milliseconds.

The cases that need a real runtime stay where they are: C# calling the engine, C# systems running, a rebuilt assembly really reloading. They are no longer also the only way to check a rule.

The public `Scripting` interface loses the two operations only tests used. They move to the test probe, which already exists for exactly this.

## User Stories

1. As a maintainer, I want the change buffer's playback rules tested without .NET, so that a failure in them is a failure in them and not in a fixture.
2. As a maintainer, I want to record a create, add, remove and destroy and play them back against a real entity list, so that I check what an app sees.
3. As a maintainer, I want to check that changes apply in the order they were recorded, so that an add followed by a remove leaves the entity without the component.
4. As a maintainer, I want to check that a change naming an entity an earlier change destroyed is skipped with a warning, so that it never reaches a slot that was reused.
5. As a maintainer, I want to check that destroying an entity twice is harmless and is not reported, so that scripts need not guard it.
6. As a maintainer, I want to check that an entity created by a script is usable at once and gets Transform, Name and Relationship only at playback, so that the documented timing holds.
7. As a maintainer, I want to check that discarding drops every recorded change and destroys the entities created and not yet played back, so that none are left behind.
8. As a maintainer, I want to check each refusal code the buffer returns (unknown component, read-only, required, entity not alive, scripting not running, unknown event), so that C# keeps getting the codes its messages are built from.
9. As a maintainer, I want to check that an event recorded by a script reaches the event's queue at playback and not before, so that a system never reads its own sends.
10. As a maintainer, I want to check that a recorded event whose queue has gone away is skipped with a warning, so that a reload in between cannot crash playback.
11. As a maintainer, I want to check that an add carrying a value copies those bytes at record time, so that the script may reuse its buffer.
12. As a maintainer, I want to test the reconcile rules over a description of what a script assembly defines, so that I do not need an assembly that lies.
13. As a maintainer, I want to check that a component reported twice is refused, with the same message as today, so that script authors get the same explanation.
14. As a maintainer, I want to check that a component with a layout no C# struct has (zero size, alignment that is not a power of two, size not a multiple of alignment) is refused, so that the ECS never stores it.
15. As a maintainer, I want to check that a script component cannot take the name of a native component, so that a script cannot shadow the engine.
16. As a maintainer, I want to check that two script components whose names share a storage id are refused, naming both.
17. As a maintainer, I want to check the same refusals for events, plus that a script event cannot take the name of a queue the app already has unless the previous generation owned it.
18. As a maintainer, I want to check that a refusal leaves nothing registered and no storage made, so that a failed load is as if it never happened.
19. As a maintainer, I want to check that a sound report registers every component and event, so that the happy path is pinned beside the refusals.
20. As a maintainer, I want to check that script data whose layout is unchanged from the previous generation stays, so that a reload keeps what it can.
21. As a maintainer, I want to check that a component whose layout changed is removed from every scene with a warning that says how many were removed, so that no system reads old bytes as a new layout.
22. As a maintainer, I want to check that a component or event that no longer exists is dropped with a warning that says it no longer exists.
23. As a maintainer, I want to check that an event queue whose layout changed is removed with its events, and one whose layout is unchanged is kept with its events.
24. As a maintainer, I want to check that the native components' and events' checks against their C# structs run after the definitions' own checks and before anything is dropped or registered, so that the all-or-nothing order is pinned.
25. As a maintainer, I want to supply the failure of that native check as data, so that "a native component does not match its C# struct" is tested without a mismatched fixture.
26. As a maintainer, I want to check that dropping all script data on shutdown or release empties what is owned, so that nothing outlives the assembly.
27. As a maintainer, I want the reload watcher's decision tested with literal times and file stamps, so that debouncing is checked without sleeping or touching a file.
28. As a maintainer, I want to check that the watcher reports Idle when called sooner than the reload interval after the last look, so that it does not poll the disk every frame.
29. As a maintainer, I want to check that a file whose stamp equals the loaded one is Idle and clears any change it was waiting on.
30. As a maintainer, I want to check that a file that cannot be looked at is Idle.
31. As a maintainer, I want to check that the first sight of a new stamp is Settling, and that a stamp that changes again while settling restarts the wait.
32. As a maintainer, I want to check that a stamp stable for less than the debounce is still Settling, and that one that cannot be opened is Settling even after the debounce.
33. As a maintainer, I want to check that a stable, readable stamp after the debounce is Ready.
34. As a maintainer, I want to check that with hot reload off the watcher is always Idle.
35. As a maintainer, I want to check that marking a generation as loaded resets what the watcher was waiting for, so that a reload by hand does not trigger a second one.
36. As an app author, I want the public `Scripting` interface to hold only what an app can use, so that I do not wonder when to call `ApplyChanges`.
37. As an app author, I want `FindComponent` to stay public, so that I can reach a registered component on an entity from native code.
38. As a maintainer, I want the end-to-end cases that need .NET to stay, so that the C# to C++ path (a real assembly reporting something the engine refuses, a real rebuild keeping its data) is still checked.
39. As a maintainer, I want one real assembly that reports something the engine refuses, kept for each kind, so that the path from the generated table through the native function to the refusal is covered end to end.
40. As a maintainer, I want the rules tests to be part of CTest and of the normal build, so that one command runs everything.
41. As a maintainer, I want the rules tests to need no environment variables and no fixture paths, so that I can run one by name from an IDE.
42. As a maintainer, I want everything a script sees (messages, codes, ordering) unchanged by the restructure, so that this is a pure refactor for script authors.

## Implementation Decisions

### Where the tests live and how they reach the modules

- A new test executable, built with the other FenrirScripting tests, that links the library, starts no .NET runtime and needs no fixture assembly or environment variable. It runs with declaration order like the others.
- The three seams below are the library's own private modules (the change buffer, the reconcile rules, the reload watcher). The new executable is given the package's private source directory on its include path, and nothing else is exposed. Per CODING_STANDARDS this is a seam agreed in the spec and kept out of the product interface. It is the only way the new tests reach internals.
- The existing headless app helper (an App with no window and a recording logger) is reused.

### Seam 1: the change buffer

- Its interface stays as it is: record a create, destroy, add, remove or event send; play back; discard. It already needs only an App and a component registry, so it is tested as it stands, with native component registrations (and a dynamic one for the script-defined kind) and a real entity list.
- No behaviour changes. This seam exists in the spec because it is already deep and testable; the work is the tests.

### Seam 2: reconcile over what the assembly reports

- Reconcile today asks the loaded Iris component for everything: it runs the generated tables, reads each type's fields for its layout fingerprint, and checks native components against their C# structs. The rules themselves (duplicates, usable layouts, name clashes, storage ids, what to drop, what to keep) are plain C++ mixed in with those questions.
- Reconcile gets a narrow interface for the assembly side: *what the script assembly reports*, with one question per thing it asks the assembly, asked in today's order:
  1. the components the assembly defines (name, size, alignment);
  2. the events it defines;
  3. a layout fingerprint for a definition;
  4. whether the native components and events match their C# structs.
- Two adapters stand behind it, so the seam is real: one over a loaded Iris component (today's code, moved), and one that returns literals for tests.
- The order of the work is unchanged: components' report and rules, then events' report and rules, then the native check, then dropping stale data, then registering. A failure at any step changes nothing, with the same message as today.
- A *script report* is the vocabulary for the data the assembly side returns: the components and events a script assembly says it defines, each with its size and alignment (and, when asked, its layout fingerprint). `GLOSSARY.md` gains the term.
- The data the previous generation left (the owned layouts) and the registry and App it acts on are passed in as today.

### Seam 3: the reload watcher's decision

- The decision "Idle, Settling or Ready" is separated from the generation lifecycle, which also loads, releases and restores generations through Iris. It becomes its own module whose inputs are: the settings (hot reload on or off, the look interval, the debounce), what the loaded generation's file looked like, the current time, and what the file looks like now (a size and modification time, or nothing) and whether it can be opened.
- Reading the file's stamp and opening it stay with the lifecycle, which supplies them. The module holds the pending state and the rules, and has no file access.
- The lifecycle keeps its guards that need a host (no host, no decision) and delegates the rest. Its behaviour, including when `Poll` is Idle, is unchanged.

### The public `Scripting` interface

- `ApplyChanges`, `DiscardChanges` and their result type are removed from `Scripting`. Nothing in the engine, the demo or the documented app flow calls them: the system runner reaches the same operations from C#. They move to the test probe, which keeps reaching the same internals as before.
- `FindComponent` stays. It is documented for app authors and the demo path uses it.
- The probe's counters stay. A script's query makes a fixed number of native calls however many entities match, which is an invariant worth a test, and the counter is the cheapest way to see it.
- The package README's "From C++" notes for these operations are removed or reworded, and the doc comments that mention them are updated in the same change.

### Which existing cases move

- Rules that are checked today through C# and can be checked at a seam move there, with their expected values unchanged: the playback order and skip cases, the discard case, the refusal codes, every refusal of a bad report (duplicate component, impossible layout, duplicate event, impossible layout, an event named like an existing queue, a script component named like a native one), the "sound report" case, the stale-data cases, and the watcher's decision rules.
- Cases whose point is that C# and C++ agree stay: C# recording changes and reading its own results, a Ref staying valid, systems running in order, events read in both languages, a real rebuild keeping unchanged data, a real broken build keeping the old generation, the watcher reloading a real stable build.
- The hand-written bad-report assembly shrinks to the modes needed for one end-to-end refusal per kind. The others, and their switch, go.

## Testing Decisions

- **A good test here** sets up the module's inputs as literals, calls its interface, and asserts what a caller can observe: return values, the state of the entity list, the registry, the event queues, the log's warnings and errors, and the codes C# is given. It names no private member. Expected values are literals (a number, a message, a list of names), not recomputed.
- Per CODING_STANDARDS: one behaviour per case, named for the behaviour, a `REQUIRE` with an `INFO` that says what went wrong, and the declaration-order note where it applies.
- **Modules tested:** the change buffer; reconcile through the literal-report adapter; the watcher module. The Iris-backed adapter is covered by the end-to-end cases that remain.
- **Characterisation first.** Each move is done as: write the rule's test against the old path (it passes), build the seam, make the test cross the seam (it still passes), then delete the C#-driven version. A rule is never left untested between steps.
- **Prior art:** the FenrirECS tests (`FenrirECSTests`, no .NET, public interface only), which this spec follows in spirit; the existing scripting cases that are being moved, whose expectations carry over; the headless app helper.
- Messages are asserted as literals. The messages already used by the C#-driven cases (for example "which no C# struct has", "more than once") are the expected values.

## Out of Scope

- Faking Iris so the generation lifecycle's load, release and restore logic can run without .NET. That needs the in-process bridge table specified in `.scratch/iris-internals`, and can follow it.
- Any change to C#, the scripting contract, the generators or what a script sees.
- Parallel C# systems, C# systems in the scheduler, and the event/App queue changes from the same review.
- Changing the reload watcher's timings or the reconcile rules. Both are moved, not altered.

## Further Notes

- Order of work: the change buffer's tests and the probe move first (no restructure needed); then the watcher module; then the reconcile seam, which is the largest.
- This is a pure refactor for script authors and for app authors who do not call the two removed operations. The existing scripting suite is the net for everything that stays.
- `.scratch/fenrirecs-tests` and `.scratch/fenrirecs-component-type` are the earlier, smaller uses of the same idea.
