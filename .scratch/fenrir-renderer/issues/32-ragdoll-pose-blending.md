# How do animated and simulated poses meet?

Type: grilling
Status: resolved
Blocked by: 27

## Question

Ragdoll glue writes simulated body poses into a skeleton as a modifier and reads the base pose to drive powered ragdolls (ADR-0028); the bone-to-body mapping and the bodies themselves are the physics map's. What does the animation side hand over?

- The first weighted override's representation for ragdolls: world-space targets resolved by finalisation, or local; per-bone weights, and setting a subtree's weight at once.
- Going limp and blending back: who ramps the weight, and what the glue writes each frame.
- Bones with no body: following their nearest mapped ancestor, or keeping the animated pose.
- Powered ragdolls: reading the base pose (local or model space) as motor targets, at which phase relative to physics ticks and interpolation.
- What the physics map's ragdoll spec receives: a note listing names, indices, hierarchy and the two poses, and that the bone-to-body mapping moved there.
- A headless test: a skeleton driven only by override targets, with no physics.

## Answer

ADR: `docs/adr/0038-ragdoll-poses-enter-as-world-pose-overrides-scaled-by-gameplay-owned-override-weights.md` (extends ADR-0033's `PoseOverride`). Glossary: Ragdoll glue, Pose override, Override weight added.

- **Write path:** glue, in `Update` after `AfterTicks`' interpolation, reads each mapped body's world transform, applies the body-to-bone offset and calls `SetOverride(entity, bone, Target { World, trs, all channels, weight 1 })`; `PoseOverride` writes in bone order (parents first, past bones with no entry), each world target converted through the parent's current model matrix, so mapped bones land on their bodies where the local is TRS without shear and the inverses exist. No new pose mechanism.
- **Ownership:** glue owns its entries, refreshing them while physics drives and clearing them when it stops; entries left unrefreshed hold the last simulated pose. `PoseOverride` is shared: gameplay and glue on one bone means the last `SetOverride` wins, documented; a dedicated ragdoll modifier is the later remedy.
- **Weights:** a per-bone override weight, default 1, gameplay's; effective weight `clamp(Target.weight × overrideWeight[bone], 0, 1)`. `SetOverrideWeight(entity, bone, w, Scope::Bone|BoneSubtree)`, `GetOverrideWeight`, `ClearOverrideWeight` (back to 1); a subtree assignment writes each bone's value, latest wins, no multiplying. Weights survive glue's updates and clears; both entries and weights are dropped on rebuild, documented (kept by bone identity later). `SkeletonData` gains `IsDescendant` and subtree iteration.
- **Ramps:** game code ramps; FenrirSkeleton has no fades. Limp is usually an instant step to 1, getting up a timed ramp to 0; glue writes live bodies or holds the last pose meanwhile, then clears at 0. A fade helper from ticket 29 may be reused, not waited on.
- **Unmapped bones:** keep their animated local pose, so they follow the nearest mapped ancestor; bones between bodies carry their animated locals (chains may bend where the mapping skips bones); bones above the root body stay animated. Glue may write more.
- **Powered ragdolls:** glue reads the base pose, never the final pose: `GetBaseLocal` and a lazily computed, cached `GetBaseModel`, valid from `Animate` to the next `Animate`, read in `Late` or before the next ticks; ticks in frame N+1 chase frame N's animation. Motor targets, strengths and body relations are the physics map's.
- **Skeleton entity:** glue or game moves it to follow the ragdoll (e.g. under the root body) in `Update`, before writing targets; the policy is the physics map's ragdoll spec's.
- **Physics map:** its ragdoll family is told what it receives (names, indices, parents, subtree query, world-target writes, override weights, base-pose reads and their timing) and what it owns (mapping, offsets, bodies, joints, motors, collision groups, follow policy); noted in `.scratch/fenrir-physics/map.md`.
- **Headless test:** in FenrirSkeleton's tests, a chain skeleton with plain `Transform` entities as fake bodies and a test glue writing world targets in `Update`: mapped bones match, unmapped children follow, skipped bones keep animated poses, subtree 0.5 blends halfway and a later per-bone weight wins, clearing entries keeps weights, a rebuild drops both, base reads ignore overrides.
- **Graduated:** no new tickets.

## Comments

- From ticket 27 (ADR-0033): the first weighted override is `PoseOverride`, an ordinary modifier at `ModifierOrder::Override` (1000): persistent `{bone, Target}` entries per instance via `SetOverride`/`ClearOverride`/`ClearOverrides`, a `Target` being `{ space Local/Model/World, trs, channels, weight }`, written in bone order and dropped when the instance is rebuilt. Subtree weights, ramps and what glue writes each frame are this ticket's. Targets are met when applied, not held; world targets need an invertible skeleton world matrix.
