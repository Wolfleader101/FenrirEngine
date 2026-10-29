# 07: the pose override and override weights let a subtree go limp and blend back, shown in the Going limp scene

**What to build:** gameplay (or ragdoll glue) sets world-space targets on bones through `PoseOverride`, scales them per bone or per subtree with override weights, and the skeleton blends between animation and the targets, proven by ADR-0038's headless handover test and AnimationDemo's scene 6 (spec: "Pose override, override weights and the ragdoll handover"; ADR-0033, ADR-0038).

- **`PoseOverride`** as an ordinary modifier at `ModifierOrder::Override`, the first modifier and not the contract: persistent `{ bone, Target }` entries per instance through `SetOverride`, `ClearOverride` and `ClearOverrides`, written in bone order parents first and skipping bones with no entry, so each world target goes through its parent's current model matrix; the last `SetOverride` on a bone winning, whoever made it.
- **Override weights:** one per bone, default 1, gameplay's; effective weight `clamp(Target.weight × overrideWeight[bone], 0, 1)` with 0 skipping the entry; `SetOverrideWeight` with `Scope::Bone` or `Scope::BoneSubtree` (a subtree assignment writing each bone's value, latest wins, no multiplying), `GetOverrideWeight` and `ClearOverrideWeight`; weights surviving the glue's updates and clears; entries and weights both dropped on destroy or rebuild (documented). No fades in FenrirSkeleton.
- **The handover contract** FenrirSkeleton documents for the physics map's ragdoll spec: names, indices, parents and the subtree query; world-target writes in `Update`; override weights; base-pose reads valid from `Animate` to the next `Animate`, a frame late on frame-posed skeletons. Bones with no entry keep their animated locals, and unmapped children follow their nearest mapped ancestor by construction of the model matrices.
- **Scene 6, Going limp**, in AnimationDemo with its README row and hand checks: a subtree override on the humanoid's arm going limp (targets written by demo code from a simple sag or sway, no physics) and blending back by ramping the weights, with the override state shown on screen.

**Blocked by:** 05

**Touches:** `packages/FenrirSkeleton/`, `examples/AnimationDemo/src/`, `examples/AnimationDemo/README.md`, `GLOSSARY.md`

**Status:** ready-for-agent

- [ ] Seam 8's ADR-0038 headless test: plain `Transform` entities as fake bodies with a test glue writing world targets in `Update`; mapped bones match their bodies; unmapped children follow; skipped bones keep their animated pose; a subtree weight of 0.5 blends halfway and a later per-bone weight wins; clearing entries keeps weights; a rebuild drops both; base reads ignore overrides.
- [ ] `PoseOverride` runs at order 1000 among other modifiers, and a modifier registered after it sees its results.
- [ ] Scene 6 is in the scene list and README with keys and hand checks; smoke runs still pass.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the arm goes limp, its fingers follow it, the rest of the body keeps animating, and ramping the weight back blends the arm into the walk without a pop.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
