# 03: Reconcile script components through the new module

**What to build:** A reconciliation module whose interface takes the engine context, registry, freshly loaded Iris component and owned layouts, and either commits the whole load or changes nothing. This ticket adds the script-component adapter (clearing means removing the dynamic type from every scene) and moves component discovery, checks, fingerprint comparison, stale-data dropping and registration into it. Events keep using the old code. The ECS library is not named outside FenrirECS (ADR-0008).

**Blocked by:** 01, 02

**Status:** ready-for-agent

- [ ] Component logic is gone from the main scripting source's load path
- [ ] Log wording for dropped data unchanged
- [ ] Ticket 01 tests and all existing tests pass unchanged

Spec: `.scratch/script-schema-reconciliation/spec.md`
