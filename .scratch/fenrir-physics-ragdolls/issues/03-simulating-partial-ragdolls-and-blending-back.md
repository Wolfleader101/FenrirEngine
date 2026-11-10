# 03: Simulating, partial ragdolls, blending back and the skeleton entity following

**What to build:** a ragdoll goes limp and gets up: `Ragdoll::mode = Simulating` (or one body's `RagdollBody::mode`) makes bodies dynamic without moving them, keeping their following velocity; each frame the glue writes the simulated bodies' poses into the skeleton as world override targets; a limp arm hangs from its following shoulder; the skeleton entity follows the hips horizontally and in yaw; setting `Following` again snaps the bodies back while gameplay's weight ramp blends the skin, and the glue clears its entries once every weight is 0 (spec: "the glue each tick and frame" (`Tick` step 2 for `Simulating`, the whole of `Update`), "the components" (lifetime); ADR-0054 with its amendments, ADR-0038).

- **Modes:** `Simulating` on the ragdoll and `RagdollBodyMode::Simulating` per body applied as `motionType = Dynamic`; `Inherit` takes the ragdoll's. Switching repositions nothing; the velocity written by ticket 02's follow carries over.
- **`Update`** (after physics's `AfterTicks`): for each simulating body, `body (interpolated world) × offset⁻¹` written as `SetOverride(skeleton, bone, Target { World, trs, Translation | Rotation, weight 1 })`; the glue never writes override weights. While every body is following, entries are kept (holding the last simulated pose) until every weight of the bones it wrote reads 0 through `GetOverrideWeight`, then cleared; `Ragdolls::ClearOverrides(scene, skeleton)` clears at once.
- **Follow root:** while any body is not following and `followRoot` is true, the skeleton entity is moved so the root body's horizontal world position and yaw land where the hips bone's base pose puts the hips relative to the entity; height kept, upright. Not moved while every body follows.
- **Partial ragdolls:** a simulating subtree under a following parent hangs from its joint to the kinematic parent; overrides for the simulating bodies' bones only.
- **Lifetime and sleep:** a body destroyed while simulating stops being written and its bone keeps its last override; `Ragdoll` removed leaves the bodies as inactive `RagdollBody`s with one warning; a sleeping body is still written each frame; `anyAwake` reports the bodies' sleep; an inactive scene's ragdoll keeps its state.
- **Docs:** on `Ragdoll::mode`, "set the weights to 1 when you drop, ramp them to 0 as you get up"; the glue never sets weights.
- **Glossary:** *ragdoll mode* with *following*, *simulating*, *partial ragdoll*; keep current.

**Blocked by:** 02 (Ragdoll tracer bullet).

**Touches:** `packages/FenrirRagdoll/`

**Status:** ready-for-agent

- [ ] `FenrirRagdollTests`: `Simulating` makes every body dynamic in place with the following velocity kept (a walking rig's bodies keep moving forward on the switch); the bodies fall onto a floor and settle.
- [ ] Each simulating body's bone world matrix (render pose) equals the body's interpolated pose × offset⁻¹ within tolerance, with weights 1; with a subtree weight 0.5 the bone is halfway; the glue never changes a weight.
- [ ] One arm's bodies `Simulating` under a following shoulder: the arm hangs within tolerance of its shoulder joint anchor and does not stretch; only its bones get overrides.
- [ ] Follow root: the skeleton entity moves horizontally and in yaw under the root body while simulating, keeps its height, does not move with `followRoot = false` nor while following.
- [ ] `Following` again snaps bodies to the pose over one tick; entries hold while any weight is non-zero and are cleared once all read 0; `ClearOverrides` clears at once.
- [ ] A body destroyed mid-simulation leaves its bone's last override; removing `Ragdoll` leaves inactive bodies with one warning; a sleeping ragdoll reads `anyAwake = false` and is still written.
- [ ] Two runs of a fall onto boxes are bit-identical over 300 ticks.
