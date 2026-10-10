# 01: Review the hosting package and record the scope decision

**What to build:** A full review of the existing FenrirScripting package (native host, managed bridge, component registry, examples, tests, CMake, README), carried out with fresh eyes because the code was written by a weaker LLM. The output is a findings report the user can act on. This ticket also records the decision that we are building a general-purpose, engine-independent hosting library comparable to Mono/Coral before any engine integration, and rewrites the implementation plan to match. The current plan forbids general reflection and marshalling, so that ban must be replaced.

**Blocked by:** None (can start immediately)

**Status:** resolved

- [x] The findings report is in this feature directory. It groups findings as correctness bugs, lifetime/ownership risks, overcomplication or duplication, test gaps, and comment/doc noise. Each finding is located and rated by severity.
- [x] The report explicitly assesses: per-export name lookup and ABI bumps, the per-method readiness flags on the initialization result, demo calls living in the production bridge, the repeated result structs, the README's size, and the `int(int,int)`-only invocation path.
- [x] Each finding is tagged with the later ticket that fixes it (02–05), or marked "new ticket needed".
- [x] An ADR records: an own Coral/Mono-style API, the core usable from any C++ project, engine integration only after a stable API, and CI deferred.
- [x] The implementation plan is rewritten to point at this ticket set and drops the outdated scope restrictions. The contracts listed in the spec are preserved.
- [x] No production code changes in this ticket.

## Comments

**Done.**
- The findings are in `../review-01.md`.
- The scope decision is recorded in `docs/adr/0001-own-dotnet-hosting-library-before-engine-integration.md`.
- `packages/FenrirScripting/IMPLEMENTATION_PLAN.md` has been rewritten to point at this ticket set.
- No production code changed. The baseline `ctest -C Debug` passes 6/6 on Windows.

Open question for the user: should review finding D4 (shared contract assemblies) become its own ticket between 05 and 06, or be folded into 05?
