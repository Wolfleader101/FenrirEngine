# 03: Pin every managed export's error contract

**What to build:** Before the export envelope is written, the behaviour it must preserve is checked on today's code (spec: "The managed export envelope" and "Characterise before moving").

The managed checks that already call the bridge's exports in process gain table-driven cases, one row per export, that pin what a caller can get: the statuses, the diagnostics, and the outputs left on failure. The table also records the exports whose exception-to-status mapping differs from the others, so the difference is a documented fact and not an accident.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] For every export family the checks pin: an invalid diagnostic tail gives `InvalidArgument` and writes nothing; a null, empty, invalid-UTF-8 or embedded-NUL name argument gives `InvalidArgument` (where the export takes a name); an argument error, a stale handle and any other exception each give the status the export gives today.
- [ ] The checks pin the outputs left after a failure (a token zeroed, a required count zero, a state of minus one, and so on) and that a diagnostic is written for a managed exception.
- [ ] The cases where the exports differ in mapping (for example a decode failure or an argument error mapped to `InvalidArgument` and everything else to `ManagedException`, with no `InvalidHandle` case) are listed in the table with the reason, as a note in the check.
- [ ] The checks are driven from one table of exports and cases, not written export by export.
- [ ] All checks pass on the unchanged bridge. The full CTest suite passes.
