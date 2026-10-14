# 09: One check that native and managed numbering agree

**What to build:** The two sides of the bridge number the same things and must agree: value kinds, snapshot type kinds, member visibilities and statuses. Today nothing checks the enum values, only the table's slot count (spec: "The managed registry split").

One literal table of the numbering lives in the tests. A native check and a managed check each read it, so each side is pinned to the same numbers and a renumbering on either side fails loudly in one place.

**Blocked by:** 06 (Managed value codec out of the registry).

**Status:** ready-for-agent

- [ ] A single table in the tests lists the value kinds, snapshot type kinds, visibilities and statuses with their numbers.
- [ ] A native check compares the native definitions with the table, and a managed check compares the bridge's definitions with it. Both are part of CTest.
- [ ] Changing any one number on either side makes a check fail with a message that names the item.
- [ ] No production behaviour changes. The full CTest suite passes.
