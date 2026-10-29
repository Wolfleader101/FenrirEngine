# 11: tick-posed skeletons evaluate their base pose each fixed tick, and a kinematic shield follows a bone in the physics step

**What to build:** setting `SkeletonPose::timing = PoseTiming::Tick` makes a skeleton's base pose evaluate in each fixed tick, so demo glue can move a kinematic shield from the simulation pose in `Tick` with no lag and the right velocity, while the drawn pose blends between ticks at physics's fraction, shown by the shield pushing dynamic boxes in AnimationDemo's scene 5 (spec: "Tick-posed skeletons", "`Animate` passes"; ADR-0040). It comes after playback, blending, overrides and attachments work per frame, because it changes all four. The C# side of tick timing is ticket 13's.

- **Tick pass** in `PreTick` on the active scene: Reconcile, `Animate` (the tick pass, with the context's `PoseTiming::Tick`, `Time::tickRate` and the tick-posed instances) and a base finalise over tick-posed instances, giving the simulation pose with no modifiers, with the previous tick's base locals kept beside it in `SkeletonPoses`. A tick pass runs only when the active scene has a tick-posed instance.
- **Clocks and events** in FenrirAnimation: advancing by `tickRate`, including dropped ticks; events firing in the tick whose step crosses them, delivered per tick; crossfade progress by the tick; the write-visibility rules (before `PreTick` seen by that tick, in `Tick` by the next, in `Update` by the next frame's first tick); a frame with no tick evaluating nothing.
- **Frame pass:** a tick-posed instance skipping `Animate`, its base pose the blend of the last two tick base locals at `Time::accumulator / Time::tickRate` (lerp and slerp) read from FenrirTime, then Prepare, `Modify`, Finalise, Attach and `Late` giving the render pose. Modifiers are render-only; the palette and attachments use the render pose.
- **Reads:** `PoseKind::Simulation` on the base reads and `GetBoneWorldMatrix` returning the tick-time pose; `Render` the blended one; both the render pose on a frame-posed skeleton (already so from ticket 03).
- **Seeding:** at spawn and on switching `Frame` → `Tick`, both tick poses seeded from the current base pose; switching back resuming frame `Animate` from where the tick clock stopped. `EditBasePose` on the other timing during `Animate` refused with one log per call site (ticket 03 declared the rule; this ticket makes the tick pass exercise it).
- **Scene 5's second part**, in AnimationDemo with its README row and hand checks: a tick-posed character whose kinematic shield, moved by demo glue in `Tick` from `GetBoneWorldMatrix(…, Simulation) × offset` into the body entity's parent space with physics's `PostTick` moving the body, pushes dynamic boxes; the app installs FenrirPhysics for this, under `FENRIR_ENABLE_PHYSICS`. The shield entity has no `BoneAttachment`. A key switches the character between timings to show the lag come and go.

**Blocked by:** 08, 09, 10

**Touches:** `packages/FenrirSkeleton/`, `packages/FenrirAnimation/`, `examples/AnimationDemo/`, `examples/CMakeLists.txt`, `GLOSSARY.md`

**Status:** ready-for-agent

- [ ] Seam 8 tick-posed cases: one evaluation per tick, the clock by `tickRate`, none in a tickless frame; the render base pose as the blend at `accumulator / tickRate`; `Simulation` reads in `Tick` matching tick-time sampling and equal to render reads on frame-posed skeletons; modifiers changing the render pose only; switching timing without a jump; `Animate` items seeing the right timing, step and instances in each pass; `EditBasePose` on the other timing refused; a test glue placing a plain entity on the simulation bone each tick.
- [ ] Seam 9: a tick-posed skeleton's clip advancing by `tickRate` with events per tick and a crossfade progressing per tick.
- [ ] Scene 5's shield is in the README with keys and hand checks; smoke runs pass with physics on and off.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the shield pushes the boxes with no visible lag on `Tick` timing and a frame of lag on `Frame` timing, the character and its shield stay together on screen between ticks, and the override and attachments from scenes 5 and 6 still behave on a tick-posed character.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
