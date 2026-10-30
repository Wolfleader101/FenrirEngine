# 09: the pose override and override weights let a subtree go limp and blend back, shown in the Going limp scene

**What to build:** gameplay (or ragdoll glue) sets world-space targets on bones through `PoseOverride`, scales them per bone or per subtree with override weights, and the skeleton blends between animation and the targets, proven by ADR-0038's headless handover test and AnimationDemo's scene 6 (spec: "Pose override, override weights and the ragdoll handover"; ADR-0033, ADR-0038).

- **`PoseOverride`** as an ordinary modifier at `ModifierOrder::Override`, the first modifier and not the contract: persistent `{ bone, Target }` entries per instance through `SetOverride`, `ClearOverride` and `ClearOverrides`, written in bone order parents first and skipping bones with no entry, so each world target goes through its parent's current model matrix; the last `SetOverride` on a bone winning, whoever made it.
- **Override weights:** one per bone, default 1, gameplay's; effective weight `clamp(Target.weight × overrideWeight[bone], 0, 1)` with 0 skipping the entry; `SetOverrideWeight` with `Scope::Bone` or `Scope::BoneSubtree` (a subtree assignment writing each bone's value, latest wins, no multiplying), `GetOverrideWeight` and `ClearOverrideWeight`; weights surviving the glue's updates and clears; entries and weights both dropped on destroy or rebuild (documented). No fades in FenrirSkeleton.
- **The handover contract** FenrirSkeleton documents for the physics map's ragdoll spec: names, indices, parents and the subtree query; world-target writes in `Update`; override weights; base-pose reads valid from `Animate` to the next `Animate`, a frame late on frame-posed skeletons. Bones with no entry keep their animated locals, and unmapped children follow their nearest mapped ancestor by construction of the model matrices.
- **Scene 6, Going limp**, in AnimationDemo with its README row and hand checks: a subtree override on the humanoid's arm going limp (targets written by demo code from a simple sag or sway, no physics) and blending back by ramping the weights, with the override state shown on screen.

**Blocked by:** 05

**Touches:** `packages/FenrirSkeleton/`, `examples/AnimationDemo/src/`, `examples/AnimationDemo/README.md`, `GLOSSARY.md`

**Status:** resolved

- [x] Seam 8's ADR-0038 headless test: plain `Transform` entities as fake bodies with a test glue writing world targets in `Update`; mapped bones match their bodies; unmapped children follow; skipped bones keep their animated pose; a subtree weight of 0.5 blends halfway and a later per-bone weight wins; clearing entries keeps weights; a rebuild drops both; base reads ignore overrides.
- [x] `PoseOverride` runs at order 1000 among other modifiers, and a modifier registered after it sees its results.
- [x] Scene 6 is in the scene list and README with keys and hand checks; smoke runs still pass.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the arm goes limp, its fingers follow it, the rest of the body keeps animating, and ramping the weight back blends the arm into the walk without a pop.
Hand check: in scene Going limp (`FENRIR_DEMO_SCENE="Going limp"`, on OpenGL), L makes the walking human's left arm hang limp from the shoulder and sway, its fingers following the hand while the rest of the body keeps walking; S stops and restarts the sway; B blends the arm back into the walk over 1.5 s without a pop, and the screen text shows the weight reaching 0 and no targets.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-30 on `claude/animation-08-blending` (ticket 08, PR #244, not merged yet), as a stacked PR. Decisions to review:

- **Where the state lives:** each `SkeletonInstance` holds `overrides` (an optional `Target` per bone), `overrideWeights` (1 by default) and a count of entries, all reset by `Rebuild`, so a handle or revision change (and destroying the instance) drops entries and weights together, as ADR-0038 documents.
- **`PoseOverride` is registered by `Skeletons::Install`** as an ordinary `Modify` item at `ModifierOrder::Override`, first in the stage's id order, so a modifier at 1000 added later, or at a higher order, sees its results. It edits only instances with entries, writing each entry in bone index order (parents first) at `clamp(weight × overrideWeight)`, skipping 0, through `PoseEdit::Write`, so world targets resolve through the parent's current model matrix.
- **The API is static, on `Skeletons`**, like the reads: `SetOverride`, `GetOverride`, `ClearOverride`, `ClearOverrides`, `SetOverrideWeight` / `GetOverrideWeight` / `ClearOverrideWeight` with `OverrideScope::Bone` or `BoneSubtree` (the spec's `Scope`, named for what it scopes), and `GetSubtree` for the handover contract's subtree query. They work on a `Ready` instance only; the test fixture `Ensure`s its skeleton so its glue's first `Update` finds it, as a spawned skeleton is.
- **No refusal inside the pipeline:** the calls are not blocked during the pose stages; ADR-0038's contract is that glue writes in `Update`, and a write later in the frame simply lands next frame.
- **Scene 6, Going limp** is fifth in the list for now, after Blending; ticket 10's Bone attachments goes between them. Its glue hangs `upperarm_l`, `lowerarm_l` and `hand_l` from the shoulder with world targets built from the base pose's world matrices (a frame late, as the contract says), each bone turned from its animated direction to the sagging one and placed at the end of its parent, translation and rotation channels only.
