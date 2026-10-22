---
status: accepted
---

# Tick-posed skeletons evaluate their base pose each fixed tick, for bodies that follow bones

Physics moves a kinematic body by reading its entity's world `Transform` in `PostTick` and calling a kinematic move towards it every step (ADR-0011, the physics foundation). ADR-0033 runs the pose pipeline once a frame in `PostUpdate`, after the ticks, and ADR-0037 places bone attachments there. So a kinematic body on an attached entity chases the pose the previous frame placed: it lags a frame. When a frame runs several ticks, it also reaches that pose on the first tick and stands still for the rest, so it pushes dynamic bodies with the wrong velocity. ADR-0037 records this as the first version's limitation and leaves the remedy to renderer map ticket 62. No synchronisation removes the lag unless the bone's pose exists at the tick's time before that tick runs. FenrirSkeleton and FenrirPhysics stay unaware of each other: they exchange plain data, and glue joins them.

The frame runs `BeforeTicks` (physics restores true body poses), then per tick `PreTick`, `Tick` and `PostTick` (physics steps), then `AfterTicks` (physics writes interpolated poses), `Update` and `PostUpdate`.

## Pose timing

- **`SkeletonPose` gains `PoseTiming timing`**, `Frame` by default or `Tick`. It stays plain and blittable, C# mirrors it, and switching is a field edit, never a structural change (ADR-0011's rule).
- **A frame-posed skeleton** is exactly ADR-0033's: the whole pipeline runs once a frame in `PostUpdate`.
- **A tick-posed skeleton** evaluates its base pose once per fixed tick, at tick time, and the frame draws a blend of the last two tick poses. It is ADR-0033's listed fixed-tick animation, built for the skeletons that opt in. It is not that design's whole answer.

## Each tick: the simulation pose

- In `PreTick`, on the active scene, FenrirSkeleton runs a tick pass over its tick-posed instances: Reconcile (ADR-0031), `Animate`, then the base pose's model matrices computed parents first. The result is the instance's **simulation pose**: the base pose at tick time, with no modifiers. The previous tick's base locals are kept beside it.
- **Clocks advance by `Time::tickRate`.** FenrirAnimation advances a tick-posed skeleton's playback by the tick, not the frame delta, so its clock moves with physics's. That includes the ticks `Time::maxTicksPerFrame` drops. Animation events fire in the tick whose step crosses them (ADR-0034).
- **Writes are seen at the next tick.** Playback commands, `EditBasePose` and anything else written before a `PreTick` is seen by that tick's evaluation. A write in `Tick` is seen by the next tick, and a write in `Update` by the next frame's first tick.
- **Several ticks** run the pass once each, and only the last two tick poses are kept. The cost is one `Animate` and one base finalisation per tick per tick-posed instance, bounded by `maxTicksPerFrame`.
- **A frame with no tick** evaluates nothing and advances no clock. The frame still blends the same two tick poses at its new fraction.

## Each frame: the render pose

- **No `Animate` for tick-posed instances.** In `PostUpdate`, the pipeline sets a tick-posed instance's base pose for the frame by blending the previous and current tick base locals. The weight is the fraction physics interpolates bodies by, `Time::accumulator / Time::tickRate`, which FenrirSkeleton reads from FenrirTime, not from physics. Translation and scale lerp and rotation slerps, as physics's interpolation does, so a body and its bone stay together on screen.
- **Then ADR-0033 as written:** Prepare, `Modify` (every modifier, `PoseOverride` and ADR-0038's ragdoll targets included), Finalise, Attach, `Late`. The final pose is the instance's **render pose**, and the skinning palette and attachments come from it.
- **Modifiers are render-only.** They run once a frame at display rate, never in the tick pass, so no weighted write is applied twice. Simulation bodies therefore never see IK or overrides. A modifier stage for the simulation pose is later work.
- **Spawning and switching.** Before an instance's first tick, both kept tick poses are its reconciled base pose. Switching `Frame` to `Tick` seeds both with the current base pose, so the pose does not jump. Switching `Tick` to `Frame` resumes frame `Animate` from where the tick clock stopped.

## Reads name the pose they want

- **`PoseKind`**, `Render` by default or `Simulation`, is an optional argument on pose reads: `GetBoneWorldMatrix`, `GetBaseLocal` and `GetBaseModel`. The palette is always the render pose's.
- **`Simulation` on a tick-posed skeleton** reads the last tick's simulation pose. A world read applies the skeleton entity's world transform at the time of the read. Read during the ticks, that is the true step pose, since physics restores it in `BeforeTicks`, so a skeleton entity under a character that physics moves in `PostTick` is consistent with its bodies.
- **On a frame-posed skeleton both kinds return the same pose.** That is the render pose, which still lags a tick reader by a frame, as ADR-0037 records.
- **No read depends on the phase it runs in.** Code that wants tick-time poses asks for them.

## Bodies that follow bones: through glue

- **Glue writes the body's `Transform` in `Tick`.** For each bone-following kinematic body, glue (app code, or a package the physics map's ragdoll spec defines) computes `GetBoneWorldMatrix(skeleton, bone, PoseKind::Simulation) × offset`. It takes that into the body entity's parent space, decomposes it to TRS and writes the entity's `Transform`. Physics's `PostTick` moves the body there over the step. Each tick gets its own target, so the body has no lag and the right velocity.
- **The body's entity carries no `BoneAttachment`.** Glue owns its `Transform`, and Attach in `PostUpdate` never overwrites it. On screen it is drawn where physics interpolates it in `AfterTicks`, at the same fraction the render pose blends by. A visible object that should match the drawn bone exactly can be a separate attached entity.
- **The body-to-bone offset** and which bodies follow which bones (hit boxes keyed by bone name) are the glue's. For ragdolls they are the physics map's ragdoll spec's.
- **Frame-posed skeletons keep ADR-0037's limitation.** A kinematic body on a `BoneAttachment` still lags a frame, and a dynamic body with a `BoneAttachment` is still unsupported. The remedy for the lag is `PoseTiming::Tick` and this glue.

## Ragdolls and dynamic bodies

- **Ragdoll targets are unchanged.** ADR-0038's glue writes world targets in `Update` from interpolated bodies, and `PoseOverride` applies them per frame in `Modify` on the render pose, whatever the timing.
- **A ragdoll's bodies are usually its hit boxes**, switched between kinematic and dynamic by `RigidBody::motionType` (ADR-0011). While kinematic they follow the simulation pose through this glue. While dynamic they drive the render pose through overrides. When each switches is the physics map's ragdoll spec's.
- **Powered ragdolls on tick-posed skeletons get motor targets at tick time.** In `Tick` they read `GetBaseLocal` or `GetBaseModel` with `PoseKind::Simulation`, which replaces ADR-0038's "a frame late". On frame-posed skeletons ADR-0038's timing stands.
- **A dynamic body following a bone** (through a joint or a spring) needs nothing more from this side. Glue keeps a kinematic anchor body on the bone as above and joins the dynamic body to it, or drives motors towards the simulation pose. The physics map owns the joint and its tuning.

## Order without new scheduling

FenrirSkeleton's tick pass runs in `PreTick`, glue in `Tick` and physics's step in `PostTick`, so each stage depends on a phase boundary rather than on `Install` order. Inactive scenes run neither pass, as in ADR-0033.

## Tests

FenrirSkeleton's tests drive an app with `RunFrame` and no physics. A tick-posed instance evaluates once per tick, its clock advances by `tickRate` per tick and not at all in a frame with no tick, and its render base pose is the blend of the last two tick poses at `accumulator / tickRate`. `Simulation` reads in `Tick` match sampling at tick time, and on a frame-posed instance they equal render reads. Modifiers change the render pose and never the simulation pose. Switching timing does not jump. A test glue writing a plain entity's `Transform` in `Tick` places it on the simulation bone each tick. The animation demo's Bone attachments scene adds a tick-posed character whose kinematic shield, moved by demo glue, pushes dynamic boxes.

## Considered options

- **Chasing the last frame pose, smoothed across the ticks.** It fixes the stall and the velocities without changing the pipeline, but it keeps the frame of lag. Rejected.
- **The whole pipeline hosted in `BeforeTicks`.** Commands written in `Update`, and ADR-0038's ragdoll targets, would show a frame late. Rejected.
- **Extrapolating the next pose from the last two.** It is wrong at every clip change and crossfade. Rejected.
- **The whole pipeline per tick, with the frame blending final locals.** Bodies would see IK and overrides, but ragdoll targets would trail their bodies by a tick and modifiers would run at tick rate. Rejected for modifiers that are render-only.
- **Drawing the latest tick pose unblended.** It steps at tick rate on displays faster than the tick. Rejected.
- **Attach run per tick for tick-posed skeletons**, so bodies could keep `BoneAttachment`. Every attachment on such a skeleton would become a tick concern. Rejected for glue.
- **A `TickPosed` marker component.** Opting in would be a structural change. Rejected for a field.
- **Reads that return the simulation pose inside tick phases.** It is the same call with a hidden difference. Rejected for an explicit `PoseKind`.

## Consequences

- *Amends ADR-0033:* `Animate` is also hosted in `PreTick` for tick-posed instances. The frame pipeline blends their tick base poses instead of animating them. The simulation pose and render pose it listed are defined here for tick-posed skeletons.
- *Amends ADR-0034:* a tick-posed skeleton's playback advances per tick by `tickRate`, and its events fire in ticks.
- *Resolves ADR-0037's lag* for tick-posed skeletons through glue. Frame-posed skeletons keep it, documented.
- *Refines ADR-0038:* powered ragdolls on tick-posed skeletons read motor targets at tick time.
- Ticket 33 mirrors `PoseTiming` in C#'s `SkeletonPose` and adds `PoseKind` to the native reads.
- The animation spec builds this, after playback, overrides and attachments work per frame.
- Listed, not built: modifiers on the simulation pose (IK on hit boxes); tick-posed pipelines for inactive scenes; ADR-0033's remaining fixed-tick questions.
