# 20: Interpolation

**What to build:** Bodies move smoothly at any frame rate (spec: "FenrirPhysics: stepping"). `RigidBody` gains `bool interpolate = true` (dynamic and kinematic bodies); `PhysicsConfig` can turn interpolation off app-wide. In `AfterTicks` physics writes `lerp(previous step pose, current step pose, accumulator / tickRate)` (slerp for rotation) into `Transform` through `SetWorldTransform`, parents before children; in `BeforeTicks` it restores the true pose, treating a `Transform` changed during `Update` as an edit (a teleport, the game's value kept). Teleports and new bodies snap.

**Blocked by:** 12 (Physics tracer bullet).

**Status:** ready-for-agent

- [ ] Tests (frame seam, frames shorter than a tick): a falling body's `Transform` between ticks lies between its last two step poses by the expected fraction; `Tick` systems and the diff see the true pose.
- [ ] Writing a body's `Transform` in `Update` teleports it and does not blend across the jump; a new body does not blend from the origin.
- [ ] A body with `interpolate = false`, and every body with the config switch off, shows step poses only.
- [ ] Children of interpolated parents end up at consistent world transforms.
- [ ] The DemoApp physics scene looks smooth on a display above 60 Hz (checked by hand).
