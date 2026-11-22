# 06: Pause and slow-motion keys in every demo app

Spec: [Pause and time scale](../spec.md). ADR-0086.

**What to build:** Every demo app gains two shared keys from the demo scaffold, listed in its help with the scaffold's others: F5 pauses and resumes, F6 cycles the scale through 1, 0.5 and 0.25 (if a scene already uses one, pick the next free function key and say so). They are on no smoke run's keys. ADR-0034's and ADR-0025's "not yet" notes on a time scale gain a line pointing to ADR-0086.

**Blocked by:** 02, 03, 04
**Touches:** `docs/adr/0086-*.md`, `examples/DemoScaffold/`, `docs/adr/0034-*.md`, `docs/adr/0025-*.md`

**Status:** ready-for-agent

Last, once 02, 03 and 04 have landed, so the hand check shows the full effect.

- [ ] Hand check in PhysicsDemo, AnimationDemo and UIDemo: F5 freezes the world while the demo UI kit, the help and the camera's UI keep working; F6 slows it.
- [ ] The scaffold's help lists both keys.
- [ ] Every demo app's smoke runs pass unchanged.
- [ ] ADR-0034 and ADR-0025 point to ADR-0086.
- [ ] ADR-0086's status becomes `accepted`.
