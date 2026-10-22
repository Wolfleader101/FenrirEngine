---
status: accepted
---

# Ragdoll poses enter as world pose overrides, scaled by gameplay-owned override weights

ADR-0028 has ragdoll glue write simulated body poses into a skeleton as a weighted pose modifier and read the base pose to drive powered ragdolls. It leaves the bone-to-body mapping, the bodies and their motors to the physics map's ragdoll spec. ADR-0033 made the first weighted override, `PoseOverride`, an ordinary modifier at `ModifierOrder::Override` (1000), with persistent `{ bone, Target }` entries per instance. It left subtree weights, weight ramps and what glue writes each frame to renderer map ticket 32. Physics never depends on FenrirSkeleton or FenrirAnimation, nor they on physics, so the animation side hands over plain data and an API that **ragdoll glue** (app code, or a package the physics map's ragdoll spec defines) uses from outside.

The frame runs `AfterTicks` (physics writes interpolated body transforms), then `Update`, then `PostUpdate`, where the pose pipeline runs.

## Simulated poses: world targets through `PoseOverride`

- **Glue writes world targets in `Update`.** For each mapped bone, glue reads its body's interpolated world transform, applies the body-to-bone offset (the mapping's, calibrated by the physics side), and calls `SetOverride(entity, bone, Target { World, trs, all channels, weight })`, usually with weight 1. FenrirSkeleton gains no new pose mechanism for ragdolls.
- **Parents first, against the current parent.** `PoseOverride` writes its entries in bone order, which is parents first, including when intermediate bones have no entry. Each world target is converted through the parent's *current* model matrix, after any earlier override of an ancestor (ADR-0033), so every mapped bone lands on its body.
- **Exact only where representable.** A bone meets its body exactly only where the needed local transform is TRS without shear in the skeleton's hierarchy (ADR-0033's shear rule), and only where the skeleton world matrix and parent model matrix are invertible.
- **Bones with no body keep their animated local pose.** Nothing is written for them. A bone below a mapped one keeps its animated local transform under its overridden parent, so it follows its nearest mapped ancestor. A bone between two bodies carries its animated local pose, and the lower body's world target still lands exactly, so the chain may bend where a mapping skips bones. Bones above the root body stay where animation puts them. Glue may write extra targets for such bones if it wants a different policy.
- **Glue owns its entries.** `PoseOverride` state persists, so glue updates its entries each frame while physics drives the pose and clears them (`ClearOverride`, `ClearOverrides`) when physics control ends. Entries it stops refreshing hold the last simulated pose, which is also how a pose freezes for blending back after bodies are removed or put to sleep.
- **Shared state, last write wins.** `PoseOverride` is public state. If gameplay and glue both set an entry for the same bone, the last `SetOverride` replaces the other. This is accepted for the first implementation and documented; a dedicated ragdoll modifier, with its own order and entries, is the remedy when layered ownership is needed.

## Override weights: how much, apart from where

- **An override weight per bone.** Each instance holds an **override weight** per bone, default 1. `PoseOverride` writes each entry with the effective weight `clamp(Target.weight × overrideWeight[bone], 0, 1)` and skips it when that is 0.
- **API.** `SetOverrideWeight(entity, bone, weight, Scope)`, `GetOverrideWeight(entity, bone)` and `ClearOverrideWeight(entity, bone, Scope)` (back to 1). `Scope::Bone` touches that bone only; `Scope::BoneSubtree` assigns the weight to the bone and every descendant.
- **Latest assignment wins.** A subtree assignment writes per-bone values; it does not multiply with earlier ones or stay attached to the subtree. Gameplay can set the whole body's weight, then override one limb.
- **Two kinds of persistent state.** Entries (where each bone should be) are glue's, refreshed every frame. Override weights (how much each entry counts) are gameplay's, and survive glue's updates and clears: refreshing or clearing an entry never resets a weight. A partial ragdoll (one limp arm) is weight 1 on that arm's subtree and 0 elsewhere.
- **Rebuilds reset them.** Override weights are dropped with the entries when the instance is destroyed or rebuilt (a handle or asset revision change), and this is documented. Preserving them across compatible rebuilds by bone identity is later work.
- **A subtree query on `SkeletonData`.** `IsDescendant(bone, ancestor)` and a subtree iteration over a bone and its descendants, for any caller, not only ragdolls.

## Going limp and blending back

- **Game code ramps.** FenrirSkeleton stores weights and has no fades: time and curves stay out of it (ADR-0034 gives playback time to FenrirAnimation). Going limp is usually an immediate step to weight 1; getting up is a timed ramp to 0 that game code advances each frame. If pose blending (ticket 29) gives a shared fade or curve helper, ramps may use it; this decision does not depend on one.
- **What glue writes each frame:** while bodies simulate, every mapped bone's world target from its body. While blending back, either the live bodies' poses or nothing (the held last pose). Once the weight reaches 0, glue clears its entries.

## Powered ragdolls: the base pose as motor targets

- **Read the base pose, never the final pose.** The final pose already holds the simulated pose and would feed back into the motors. FenrirSkeleton exposes the base pose read-only outside the pipeline: `GetBaseLocal(entity, bone)`, and `GetBaseModel(entity, bone)`, computed lazily from the base locals and cached until the base pose changes. Model space is needed when one body spans several bones.
- **Valid from `Animate` to the next `Animate`.** Glue reads it in `Late`, or in any phase before the next frame's ticks, and hands physics its targets. The ticks of frame N+1 chase frame N's animation, a frame late, which is accepted. Sampling animation at tick time needs fixed-tick animation (ADR-0033, listed).
- **Motors are physics's.** Turning base-pose transforms into joint motor targets and strengths, and how bodies or their parents relate, is the physics map's ragdoll spec's.

## The skeleton entity follows the ragdoll

World targets land correctly wherever the skeleton entity is. Drawing, attachments, anything following the entity and getting up want it near the body, so glue or game code moves the skeleton entity in `Update`, before writing targets (for example, its position under the root body). The policy is the physics map's ragdoll spec's; FenrirSkeleton does nothing for it.

## Headless test

FenrirSkeleton's tests cover this with no physics: a chain skeleton, plain entities with `Transform`s standing in for bodies, and a test glue that writes world targets from them in `Update`. Mapped bones' world matrices match their bodies; an unmapped child keeps its local transform under its mapped parent; a skipped bone keeps its animated pose; a subtree weight of 0.5 blends halfway and a later per-bone weight wins; clearing entries leaves weights; a rebuild drops both; base-pose reads after `Animate` ignore overrides.

## Considered options

- **Glue's own modifier** (`AddModifier("Ragdoll", …)`) reading bodies inside `Modify`. It would give ragdolls their own order and entries, but every app would copy `PoseOverride`'s resolution. Later, if ownership conflicts need it.
- **Local targets computed by glue** relative to the parent body. Wrong where a body spans several bones, and duplicates the conversion FenrirSkeleton already does. Rejected.
- **Only `Target.weight`.** It mixes where the body is with how limp the character is, in one call. Rejected.
- **One weight per instance** (Godot's influence). No partial ragdolls. Rejected.
- **Subtree weights that multiply or stay attached to the subtree.** Harder to reason about than latest-assignment-wins. Rejected.
- **Timed fades in FenrirSkeleton.** Puts time and curves into the skeleton package. Rejected.
- **Glue writing every unmapped bone.** More writes, and it freezes fingers. Rejected as a default; glue may still do it.
- **Motor targets from the final pose.** Feeds the simulation back into itself. Rejected.

## Consequences

- *Extends ADR-0033's `PoseOverride`* with override weights and their API; ADR-0033's entries, bone order and rebuild rule are unchanged.
- `SkeletonData` gains a subtree query; FenrirSkeleton gains base-pose reads (`GetBaseLocal`, lazy `GetBaseModel`).
- The physics map's ragdoll spec receives: bone names, indices and parents from `SkeletonData`, the subtree query, world-target writes through `PoseOverride`, override weights, base-pose reads, and the timing above. The bone-to-body mapping, offsets, bodies, joints, motors, collision groups and the skeleton entity's follow policy are its own.
- Listed, not built: a dedicated ragdoll modifier with its own ownership; override weights kept across compatible rebuilds; tick-time motor targets with fixed-tick animation.
