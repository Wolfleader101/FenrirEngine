# 19: Engine integration spec

**What to build:** A grilling session with the user that produces a separate spec and ticket set for integrating the stable hosting API into FenrirEngine: script discovery, script lifetime versus entity lifetime, the engine API surface exposed through internal calls, ECS bindings, editor reload, and threading. No implementation happens here.

**Blocked by:** 18

**Status:** resolved

- [x] An integration spec exists in its own feature directory, built only on the frozen public API.
- [x] Open questions about threading, concurrency and hot-reload are either resolved or recorded as their own tickets.
- [x] Any gaps in the hosting API that integration needs are filed as hosting tickets, not worked around inside the engine.

## Comments

### Resolution

Grilling session on 2026-10-11:
- Spec: `.scratch/engine-scripting/spec.md`. Tickets: `.scratch/engine-scripting/issues/01`–`13`.
- Decision: ADR-0007 (ECS-first C# scripting). Vocabulary: `GLOSSARY.md` "FenrirEngine scripting".
- Threading: main thread only for v1; parallel systems are engine-scripting 12 (needs-triage), including T2.
- Hot reload: designed in engine-scripting 10.
- Hosting gaps filed as Iris tickets: 20 (bool/char/enums/inline arrays; covers G3), 21 (explicit struct-mapping validation), 22 (member enumeration and attributes; G1), 23 (unload wait; G4).
- Review-18 items:
  - E4 (friendly wrappers) is engine-side, in engine-scripting 02.
  - P1 (call budget) needs no work: the design costs one call per phase and per query, so faster invocation stays deferred.
