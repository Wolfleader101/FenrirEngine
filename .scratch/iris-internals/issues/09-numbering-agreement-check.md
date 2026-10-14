# 09: One check that native and managed numbering agree

**What to build:** The two sides of the bridge number the same things and must agree: value kinds, snapshot type kinds, member visibilities and statuses. Today nothing checks the enum values, only the table's slot count (spec: "The managed registry split").

One literal table of the numbering lives in the tests. A native check and a managed check each read it, so each side is pinned to the same numbers and a renumbering on either side fails loudly in one place.

**Blocked by:** 06 (Managed value codec out of the registry).

**Status:** resolved

- [x] A single table in the tests lists the value kinds, snapshot type kinds, visibilities and statuses with their numbers.
- [x] A native check compares the native definitions with the table, and a managed check compares the bridge's definitions with it. Both are part of CTest.
- [x] Changing any one number on either side makes a check fail with a message that names the item.
- [x] No production behaviour changes. The full CTest suite passes.

## Comments

Done. `Tests/numbering.txt` is the one table: value kinds, snapshot type kinds, member visibilities, wire statuses, log levels and unload states, plus the native-only statuses. The native check (`Numbering-native`, in the fake checks executable) compares the table with a written-out list of the native definitions in both directions, so an item added to or missing from one side fails by name; the managed check (the "numbering" group of `ManagedContracts`, which gets the path from `IRIS_NUMBERING_FILE`) does the same by reflection over the bridge's enums, so a new managed member is caught without anyone remembering to list it. Mutations (the native `Internal` visibility and the managed `Public`) each fail with the item named.

One production touch, no behaviour change: the constant for the wire status 4 moved from `ManagedDiagnosticBuffer.hpp` to `BridgeFunctions.hpp`, with the other wire definitions, so the numbering check can name it without pulling in the .NET host headers. Not in the table: the numbers of the type queries, which the native side writes as literals (0 to 5) and the managed side as an enum; they would need a named native constant first.
