---
status: accepted
---

# Pose stages run as FenrirSkeleton's pose pipeline, and modifiers edit a live final pose in order

ADR-0028 orders each frame's pose work as base pose, then modifiers, then finalisation, hosted in `PostUpdate` for now, and has modifiers aim in local, model or world space. It left how that is built to renderer map ticket 27. FenrirScheduler offers phases and registration order only: within `PostUpdate`, systems run in `Install` order, with no before or after. C# systems pick a `Phase` and an `Order`. Physics steps the active scene only, and the renderer reads only the active scene. FenrirSkeleton therefore runs the stages itself, as one pipeline whose order does not depend on `Install` order, and gives modifiers a controlled, ordered view of the pose as it is being built.

## The pose pipeline

- **FenrirSkeleton owns the order.** It registers one sequential system and runs the **pose pipeline** inside it, on the active scene only. Its steps, in order:
  1. **Reconcile** (internal): instances created, rebuilt and swept (ADR-0031).
  2. **`Animate`** (public stage): base-pose writers run. FenrirAnimation adds its sampler here; code may pose through `EditBasePose`.
  3. **Prepare** (internal): every `Ready` instance's final pose is set from its base pose.
  4. **`Modify`** (public stage): pose modifiers run, in order (below).
  5. **Finalise** (internal): the model-space matrices still invalid are computed parents-first.
  6. **Attach** (internal): bone attachments are placed (their details are ticket 31's).
  7. **`Late`** (public stage): consumers that need this frame's final pose, such as a camera following a bone or debug drawing of bones.

  The skinning palette is not a step: it is computed lazily (see *Finalisation and cost*).
- **Pose stages are FenrirSkeleton's names, not app phases.** Each public stage has a host phase; the first implementation hosts the whole pipeline in `PostUpdate`. A stage can later be hosted elsewhere (`Animate` in `Tick`) without changing who writes what. How C# adds work to a stage is ticket 33's.
- **Ordering inside a stage.** Work is added to a stage with an `int` order and runs sorted by it; equal orders keep registration order (`Install` order, then call order), which is deterministic for one app and build. Relying on equal orders across independently installed packages is discouraged: a package that needs to run before or after another uses an explicit order constant.
- **Only the pipeline is ordered.** FenrirSkeleton promises order within the pose pipeline only. Other `PostUpdate` systems run before or after the whole pipeline by `Install` order, and nothing more is promised about them.
- **Active scene only.** An inactive scene's pipeline does not run: its poses and its animation clocks stay where they stopped and resume without catching up on elapsed time. Any other policy is the application's, set explicitly.

## What each stage may write

- **Before the pipeline** (`Update` and earlier): game code writes playback commands, override targets and weights, and may call `EditBasePose`. Tick phases see the previous frame's final pose.
- **`EditBasePose` outside `Animate`.** Called before the pipeline, the change is seen by the next evaluation unless an animator overwrites it at `Animate`. Called after the pipeline has run, it does not change the current frame's final pose; it is seen by the next frame's. The base pose is retained between frames, so a skeleton posed by hand with no animator keeps its pose, copied into the final pose at every Prepare.
- **`Animate`:** base-pose writers only.
- **`Modify`:** modifiers only, and only through `PoseEdit`.
- **`Late`:** strictly read-only for skeleton pose data. It may write ordinary ECS state, such as a camera's `Transform`, but never changes a pose after finalisation.

## Modifiers

- **A modifier is ordered code over a live pose.** `Skeleton::AddModifier(name, order, fn)` registers it in `Modify`. It runs once per frame whether or not any pose is dirty, since its inputs can change on their own. It receives the scene and a context, chooses the instances it acts on (usually by querying its own components), and opens `PoseEdit edit = ctx.Edit(entity)` on each.
- **`PoseEdit` is the only way in.** It offers `Base()`, `Local(bone)`, `Model(bone)` and `Write(bone, Target)`, and no mutable spans over FenrirSkeleton's arrays. An edit is valid only during the modifier call that opened it and cannot be kept across calls or frames. `Edit` on an instance that is not `Ready` returns an empty edit that writes nothing.
- **Opening an edit marks the instance modified** this frame, so finalisation cannot skip it. An edit opened with no write may cost a needless recomputation; tracking actual writes is a later optimisation.
- **A target** is `Target { space, trs, channels, weight }`: `space` is `Local`, `Model` or `World`; `trs` a translation, rotation and scale; `channels` a mask of `Translation | Rotation | Scale` (all three by default); `weight` clamped to [0,1].
- **Each write resolves at once, in order.** A write is converted into the bone's local space and blended into its current local transform, so the next write, and the next modifier, see the result:
  - a `World` target is taken into model space by the inverse of the skeleton entity's world matrix, read once per instance when `Modify` starts;
  - a `Model` target is taken into local space by the inverse of the parent bone's *current* model matrix (identity for a root);
  - the blend is in local space: translation and scale lerp, rotation nlerps along the shortest arc, for the masked channels only.
- **Satisfied when applied, not held.** A model- or world-space target is met when it is written. A later write that moves an ancestor moves the bone with it. A constraint that must hold in the final pose is a modifier ordered after those that move its hierarchy.
- **Writes are sequential.** Several writes to one bone, within one modifier or across modifiers, each apply to the result of the one before; none simply replaces another.
- **Model matrices stay coherent lazily.** A write invalidates the model matrices that depend on it; `Model(bone)` recomputes what it needs on read. Nothing evaluates the whole skeleton after every write.
- **Degenerate transforms.** A `World` target needs an invertible skeleton world matrix, and a `Model` or `World` target an invertible parent model matrix. When the inverse is unavailable, the target is skipped and a warning names the skeleton entity and bone, rate-limited so that a degenerate transform held for many frames does not flood the log.
- **Shear.** A target is exact only when the local transform it needs is representable as TRS without shear, within floating-point precision and interpolation. Under a parent with non-uniform scale and rotation, the conversion's result is decomposed into TRS approximately, and shear is discarded.

## The first modifier: a weighted override

- **`PoseOverride` is an ordinary modifier**, registered by FenrirSkeleton through the public `AddModifier` at a named order constant, `ModifierOrder::Override` (1000), so other modifiers can run before or after it. It is the first modifier, not the contract: the contract is the pose pipeline, `AddModifier`, `PoseEdit` and `Target`, and later modifiers (layers and masks, IK, procedural, ragdoll) may generalise or replace the override.
- **State:** zero or more `{ bone, Target }` entries per instance, held by FenrirSkeleton. They persist until changed or cleared, through `SetOverride(entity, bone, Target)`, `ClearOverride(entity, bone)` and `ClearOverrides(entity)`.
- **Each frame** it writes its entries in bone order; an entry of weight 0 is skipped.
- **Instance lifetime.** An instance's overrides are dropped when the instance is destroyed or rebuilt (a handle or asset revision change), so stale bone indices never land on different bones.
- Subtree weights, weight ramps and what ragdoll glue writes are ticket 32's.

## Finalisation and cost

- **Per `Ready` instance:** Prepare copies the base pose into the final pose; Finalise computes the remaining invalid model matrices in one parents-first pass; Attach places attachments. All are linear in bones (or attachments).
- **Skipping.** An instance whose pose is not dirty and that no modifier edited skips Prepare and Finalise.
- **Pose changes and moves are apart.** Moving the skeleton entity changes no model matrix and no palette: it affects only world queries (`GetBoneWorldMatrix`) and attachments, which Attach places every frame.
- **The palette is lazy.** `GetPalette(entity)`, main thread only, computes model × inverse bind the first time it is read after a pose change and caches it by the instance's pose generation. A skeleton nothing skins never pays for one, and FenrirSkeleton never needs to know what skins it.
- **Parallel later.** Instances are independent, so Prepare, Finalise and the palette may later run across the thread pool; it is not promised.

## Considered options

- **New FenrirApp phases** (`Animate`, `PoseModify`, `PoseFinalise` between `PostUpdate` and `Render`). C# would get them through `Phase` for free, but animation's words would join the core app's phase enum. Rejected.
- **Before/after labels in FenrirScheduler.** General, but engine-wide work beyond this map; the pipeline needs order only among its own stages. Not now.
- **Modifiers that only submit targets, resolved together at finalisation.** Simpler, but a modifier could never read another's output (IK after a ragdoll blend). Rejected.
- **Model- or world-space targets held against later ancestor moves.** It would make each write a lasting constraint, with an order of its own to define. Rejected: ordering modifiers is the way to keep a constraint.
- **Last write wins per bone.** Rejected: weighted writes compose sequentially.
- **The palette computed in finalisation for every instance**, or for instances a consumer flags. Rejected: lazy computation needs no knowledge of who skins.
- **Running every loaded scene's pipeline.** Not now: inactive scenes pause, as physics's do.

## Consequences

- *Amends ADR-0028*, whose finalisation "resolves [modifiers] into final local transforms": modifiers resolve their own targets in `Modify` through `PoseEdit`; finalisation computes the model matrices left and places attachments, and the palette is computed lazily on read.
- ADR-0031's reconcile is the pipeline's first step, and its "final pose is written only by modifiers and finalisation" holds: Prepare sets it, modifiers edit it.
- Tickets 28, 29, 31, 32 and 33 build on this; notes are appended to each.
- *Amended by ADR-0036:* layers and masks are not modifiers; they are blended into the base pose in `Animate`.
- *Refined by ADR-0039:* C# systems join pose stages as extension stages, at their order among C++ work; stage registration returns a handle that removes the work outside the pipeline.
- Listed, not built (in *Not yet specified*):
  - **Fixed-tick animation.** `Animate` hosted in `Tick`, with interpolation between tick poses for the frame as a starting point, not a permanent design. A real design defines a **simulation pose** (fixed-tick, authoritative for physics, hit boxes and gameplay) against a **render pose** (interpolated per frame for drawing), tick-time evaluation, interpolation, and how modifiers act on each. Today's per-frame final pose is a render pose and is not promised to serve every simulation purpose.
  - **IK between animation and skinning.** IK is a modifier in `Modify`, ordered after those that move its chain. Its inputs can be targets written in `Update`, and later solvers may query scene transforms or physics through their own integration layer, provided their stage ordering stays safe and deterministic. FenrirSkeleton itself never calls physics.
  - **Physics-driven poses.** Ragdoll bodies interpolated in `AfterTicks` feed a per-frame modifier (ticket 32). If tick gameplay needs authoritative bone positions, tick-based evaluation is added rather than making gameplay read interpolated render poses.
  - Tracking actual writes and dirty subtrees, modifier dependencies, and running inactive scenes' pipelines.
