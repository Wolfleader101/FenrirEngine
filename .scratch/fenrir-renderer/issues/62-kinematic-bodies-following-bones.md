# How do bone-following kinematic bodies get this frame's pose before the physics step?

Type: grilling
Status: resolved
Blocked by: 31, 32

## Question

A kinematic body on an entity placed by a `BoneAttachment` (a hit box, a held shield) reads its `Transform` at physics's fixed ticks, but Attach places it in `PostUpdate`, after the ticks, so the body lags a frame (ADR-0037). That lag is the first version's known limitation, not the intended behaviour. What synchronisation replaces it, with FenrirSkeleton and FenrirPhysics still unaware of each other?

- Where the pose a tick needs comes from: evaluating attachments (and the poses under them) before the ticks, a pose evaluated per tick, or interpolating between the last two evaluated poses at each tick's time.
- Who writes the kinematic target: glue reading `GetBoneWorldMatrix` and calling physics's kinematic move, rather than the attached `Transform`, and in which phase relative to `Tick` and `AfterTicks`.
- How this sits with ADR-0033's later fixed-tick animation (a simulation pose apart from the render pose) and with ticket 32's ragdoll glue, which reads bodies after the ticks.
- What it means for several ticks in one frame, and for none.
- Whether a dynamic body following a bone (through a joint or a spring, not a `BoneAttachment`) needs anything from this side.
- The physics map's share: hit boxes keyed by bone name belong with its ragdoll spec; what it is handed.

## Answer

ADR: `docs/adr/0040-tick-posed-skeletons-evaluate-their-base-pose-each-fixed-tick-for-bodies-that-follow-bones.md` (amends ADR-0033 and ADR-0034, resolves ADR-0037's lag for tick-posed skeletons, refines ADR-0038). Glossary: Pose timing, Simulation pose, Render pose added; Pose pipeline and Pose stage updated.

- **Pose source: tick-posed skeletons**, like Unity's Animate Physics. `SkeletonPose` gains `PoseTiming timing` (`Frame` by default, or `Tick`), a field edit. In `PreTick` a tick-posed instance runs Reconcile, `Animate` (its clock advances by `Time::tickRate`, and events fire in ticks) and a base finalise. That is its **simulation pose**: the base pose at tick time, with no modifiers. The last two tick poses are kept. Frame-posed skeletons are ADR-0033's unchanged.
- **Render pose:** in `PostUpdate` a tick-posed instance skips `Animate`. It blends the last two tick base locals by `Time::accumulator / Time::tickRate`, physics's own fraction read from FenrirTime (lerp for translation and scale, slerp for rotation). Then it runs Prepare, `Modify`, Finalise, Attach and `Late` as ADR-0033 describes. Modifiers are render-only, so no weighted write is applied twice and bodies never see IK. A simulation-side modifier stage is listed.
- **Reads:** an optional `PoseKind` (`Render` by default, or `Simulation`) on `GetBoneWorldMatrix`, `GetBaseLocal` and `GetBaseModel`; the palette always uses the render pose. On frame-posed skeletons both kinds return the same pose. No read depends on the phase it runs in.
- **Who writes the target:** glue, in `Tick`, computes `GetBoneWorldMatrix(…, Simulation) × offset` into the body entity's parent space and writes its `Transform`. Physics's `PostTick` moves the kinematic body there. Each tick gets its own target, so there's no lag and velocities are right. The body's entity carries no `BoneAttachment`, and physics's `AfterTicks` interpolation draws it at the render pose's fraction. Ordering comes from phase boundaries (`PreTick` pose, `Tick` glue, `PostTick` step), not from `Install` order.
- **Several ticks / none:** one evaluation per tick, bounded by `maxTicksPerFrame`. A tickless frame evaluates nothing and advances no clock, and still blends the same two poses at its new fraction. Writes in `Tick` reach the next tick; writes in `Update` reach the next frame's first tick. Spawning or switching to `Tick` seeds both tick poses with the current base pose, so nothing jumps.
- **ADR-0033's fixed-tick animation:** this is its first form, for skeletons that opt in. It defines the simulation pose and render pose for them. Ticket 32's ragdoll glue is unchanged: overrides are written in `Update` and applied on the render pose. Powered ragdolls on tick-posed skeletons read motor targets at tick time through `Simulation` reads.
- **Frame-posed skeletons** keep ADR-0037's documented lag for kinematic bodies on attachments. Dynamic bodies with a `BoneAttachment` stay unsupported.
- **Dynamic bodies following a bone** need nothing more from this side. A kinematic anchor that follows through this glue, joined to the dynamic body, or motors aimed at the simulation pose, are physics's.
- **Physics map's share:** noted in `.scratch/fenrir-physics/map.md`. Hit boxes keyed by bone name, body-to-bone offsets, where the glue lives, and when a ragdoll's bodies switch between kinematic (following the simulation pose) and dynamic (driving overrides) belong to its ragdoll spec.
- **Scope:** built by the animation spec, after playback, overrides and attachments work per frame. The Bone attachments scene adds a tick-posed character whose kinematic shield pushes dynamic boxes, glued by demo code. Ticket 33 mirrors `PoseTiming` and `PoseKind`.
- **Graduated:** no new tickets.

## Comments

- From ticket 31 (ADR-0037): the first version documents the one-frame lag of kinematic bodies on attached entities and the unsupported case of a dynamic body with a `BoneAttachment` (Attach in `PostUpdate` overwrites it). The synchronisation this ticket settles goes through glue, adding no dependency between FenrirSkeleton and FenrirPhysics.
