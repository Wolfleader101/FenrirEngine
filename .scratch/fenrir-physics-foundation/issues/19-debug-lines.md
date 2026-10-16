# 19: Debug lines in the DemoApp

**What to build:** The developer can see what physics sees (spec: "FenrirPhysics: debug lines", "DemoApp"). FenrirPhysics generates `DebugLine { from, to, colour }` from its own data (not Jolt's debug renderer) in four bit-flag categories: colliders (coloured by motion type, dimmed when sleeping, triggers distinct), contacts (point and normal), velocities, bounds. Off by default, enabled per world, regenerated after each step and read as a span valid until the next step. The DemoApp draws them in one line pass and cycles categories with a key.

**Blocked by:** 17 (Triggers and contact events); 18 (Queries).

**Status:** resolved

- [ ] Tests: with debug draw off nothing is generated; enabling `Colliders` for one box gives its twelve edges inside its bounds; each category produces lines only when enabled; a sleeping body's lines use the dimmed colour.
- [ ] Debug lines are produced in every build configuration.
- [ ] In the DemoApp physics scene, a key cycles the categories and the lines line up with the bodies (checked by hand).

**Resolved by:** merge of impl/19-debug-lines (72d97cd)

**Hand checked (2026-10-16):** DemoApp physics scene: F2 cycles colliders (grey ground, green boxes dimmed when asleep, blue paddle, grey backpack mesh), contacts (red), velocities (yellow), bounds (orange), all, off; frame rate stays acceptable with the backpack mesh drawn.
