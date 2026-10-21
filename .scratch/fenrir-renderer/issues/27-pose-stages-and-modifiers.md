# What are the pose stages, and how do pose modifiers plug in?

Type: grilling
Status: resolved
Blocked by: 26

## Question

ADR-0028 orders each frame's pose work as base pose (animation), then modifiers, then finalisation, all in `PostUpdate` for now, and lets modifiers aim in local, model or world space. How is that built so later stages need no redesign?

- How the stages are named and ordered against other packages' systems (FenrirScheduler's ordering, or sub-phases of `PostUpdate`), and what game code may write in each.
- The modifier interface: how a modifier supplies a target in local, model or world space with a weight, and how finalisation resolves the targets into final local transforms in order.
- The first implementation's single weighted per-bone override: its shape, and how it is documented as initial rather than the contract.
- Finalisation's steps and their cost: model-space matrices parents-first, the palette, attachments; skipping the palette for skeletons nothing skins.
- What would move for fixed-tick animation, IK between animation and skinning, and physics interpolation (listed, not built).

## Answer

ADR: `docs/adr/0033-pose-stages-run-as-fenrirskeletons-pose-pipeline-and-modifiers-edit-a-live-final-pose-in-order.md` (amends ADR-0028's finalisation line). Glossary: Pose pipeline, Pose stage, Pose edit, Pose target added; Pose modifier revised.

- **Facts found:** FenrirScheduler has phases and registration order only (`Install` order inside `PostUpdate`, no before/after); C# systems take `Phase` and `Order`; physics steps and the renderer reads the active scene only.
- **Stages:** FenrirSkeleton runs one sequential system, the pose pipeline, on the active scene: Reconcile, `Animate`, Prepare (final ← base), `Modify`, Finalise (remaining model matrices), Attach, `Late`. `Animate`, `Modify` and `Late` are public, the rest internal; stages are FenrirSkeleton's names, each with a host phase, all in `PostUpdate` for now. Work in a stage sorts by an `int` order, ties by registration order; equal orders across packages are discouraged in favour of order constants; nothing is promised against other `PostUpdate` systems.
- **Writes:** game code writes before the pipeline (`Update`); `Animate` base-pose writers only; `Modify` modifiers through `PoseEdit` only; `Late` read-only for poses (may write ordinary ECS state). `EditBasePose` before the pipeline is seen by the next evaluation unless an animator overwrites it; after it, by the next frame's; a hand-posed skeleton keeps its retained base pose. Inactive scenes pause and resume with no catch-up.
- **Modifiers:** `Skeleton::AddModifier(name, order, fn)`, run every frame; the modifier picks its instances and opens `PoseEdit` (`Base`, `Local`, `Model`, `Write`), valid only during its call, empty for instances not `Ready`; opening marks the instance modified. `Target { space Local/Model/World, trs, channels, weight }`; each write resolves at once (world through the inverse skeleton world matrix read at `Modify`'s start, model through the parent's current model matrix), blended in local space (lerp, shortest-arc nlerp), masked by channel; satisfied when applied, not held against later ancestor moves; writes to one bone compose sequentially; model matrices invalidated and recomputed lazily on read. Non-invertible inverses skip the target with a rate-limited warning naming entity and bone; exact only when the needed local is TRS without shear, else decomposed approximately, shear dropped.
- **First modifier:** `PoseOverride`, an ordinary modifier at `ModifierOrder::Override` (1000); persistent `{bone, Target}` entries per instance (`SetOverride`, `ClearOverride`, `ClearOverrides`), written in bone order, weight 0 skipped, dropped when the instance is destroyed or rebuilt; documented as the first modifier, not the contract.
- **Finalisation and cost:** linear per instance; clean, unedited instances skip Prepare and Finalise; skeleton entity moves touch only world queries and attachments; the palette is lazy, main-thread `GetPalette` cached by pose generation; per-instance parallelism allowed, not promised.
- **Later (listed):** fixed-tick animation with simulation pose against render pose (two-pose interpolation only a start); IK as a modifier, later free to query scene or physics through its own integration layer; ragdoll bodies interpolated in `AfterTicks` feeding a per-frame modifier, tick-based evaluation if gameplay needs authoritative bones.
- **Graduated:** no new tickets; notes appended to tickets 28, 29, 31, 32 and 33.

## Comments

- From ticket 30 (ADR-0035): FenrirSkeleton gains `PoseGeneration(entity)`, a `u64` that finalisation advances when the finalised pose changes or the instance is rebuilt, drawn from one counter shared by `SkeletonPoses` so an instance recreated under a recycled entity never repeats a generation; moving the skeleton entity's `Transform` does not advance it. The palette is built lazily for the current generation and cached with the generation it was computed for; reading it never changes the generation, and skeletons nothing skins never build one. The renderer deduplicates its per-frame upload by `(scene, skeleton entity, PoseGeneration)`.
