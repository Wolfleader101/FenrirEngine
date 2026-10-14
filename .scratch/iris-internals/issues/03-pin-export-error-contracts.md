# 03: Pin every managed export's error contract

**What to build:** Before the export envelope is written, the behaviour it must preserve is checked on today's code (spec: "The managed export envelope" and "Characterise before moving").

The managed checks that already call the bridge's exports in process gain table-driven cases, one row per export, that pin what a caller can get: the statuses, the diagnostics, and the outputs left on failure. The table also records the exports whose exception-to-status mapping differs from the others, so the difference is a documented fact and not an accident.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] For every export family the checks pin: an invalid diagnostic tail gives `InvalidArgument` and writes nothing; a null, empty, invalid-UTF-8 or embedded-NUL name argument gives `InvalidArgument` (where the export takes a name); an argument error, a stale handle and any other exception each give the status the export gives today.
- [x] The checks pin the outputs left after a failure (a token zeroed, a required count zero, a state of minus one, and so on) and that a diagnostic is written for a managed exception.
- [x] The cases where the exports differ in mapping (for example a decode failure or an argument error mapped to `InvalidArgument` and everything else to `ManagedException`, with no `InvalidHandle` case) are listed in the table with the reason, as a note in the check.
- [x] The checks are driven from one table of exports and cases, not written export by export.
- [x] All checks pass on the unchanged bridge. The full CTest suite passes.

## Comments

Done. `Tests/ManagedChecks/ExportContractChecks.cs` is one table of the 30 exports that return a status (`FreeString` returns nothing and cannot fail), run as the group "export contracts" of `ManagedContracts`. A row holds the call, what it leaves in its output after a failure (a handle zero, a count zero, a state of -1, a length zero), its exception mapping and a note of what is particular to it. The cases run from the table: four bad diagnostic tails (no count, no flag, a negative capacity, no buffer with a capacity) give InvalidArgument and write nothing; a null, empty, non-UTF-8 and null-character name for the exports that take one; the argument the export rejects before a handle; and a stale token, which is InvalidHandle with no diagnostic because it is a status and not an exception.

Differences between exports are data in the table (`Mapping`): the registry-backed exports that return a status for a stale handle never throw one, so their exceptions are all ManagedException; the lookups map a bad name or kind to InvalidArgument and anything else to ManagedException; `RegisterStruct` does not reject a null character in its description itself. `LoadComponent` and `RegisterSharedAssembly` are also run on a file that cannot be read: ManagedException with a diagnostic and the outputs left clear.

Not covered, and why: "any other exception" for the other exports needs component code that throws, which the existing groups (invocation, objects, members) already pin; the table does not repeat it. Mutations (a stale token answered NotFound instead of InvalidHandle) fail the table, and a mutation to RegisterSharedAssembly empty-path handling is equivalent because the registry rejects the empty path too.

Review follow-ups: `Mapping` and `Reason` are stated in the check as recorded from the source, not run (an exception from component code needs a component); two mapping labels were corrected (LoadComponent and RegisterInternalCall both map undecodable text to InvalidArgument and the rest by exception kind). The bytes of the diagnostic buffer are not compared on a bad tail; only the written count and truncation flag, which is what a caller reads.
