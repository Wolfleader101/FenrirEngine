# 01: Pin the load-time behaviour through the existing seam

**What to build:** Tests over `Scripting::Install` / `Reload` (headless app, fixture v1/v2 assemblies) for the load-time rules not yet covered, each run for script components and for events. They pass against today's code and guard the refactors that follow.

**Blocked by:** None (can start immediately)

**Status:** ready-for-agent

- [ ] Duplicate name refused with a message naming it (components and events)
- [ ] Invalid size or alignment refused (components and events)
- [ ] Script component colliding with a native component name refused
- [ ] Refused load leaves registry, ECS and App unchanged
- [ ] Native struct mismatch fails the whole load
- [ ] Existing tests untouched and passing

Spec: `.scratch/script-schema-reconciliation/spec.md`
