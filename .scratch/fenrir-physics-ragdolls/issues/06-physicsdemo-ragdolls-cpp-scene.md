# 06: PhysicsDemo: the Ragdolls (C++) scene

**What to build:** the PhysicsDemo shows the whole life of a ragdoll in C++: a tick-posed Quaternius human walking a lane on a character body, its ragdoll following as hit boxes; keys drop it with a shove, make an arm limp and blend it back, power it on the idle clip with strength stepping, get it up with a weight ramp, cast a ray that logs the bone hit, drop a bystander ragdoll in another group and a crate on the head; readouts in names; an autopilot for the smoke run; the README's hand-check list (spec: "PhysicsDemo: the two ragdoll scenes", "Prerequisites" (the demo assets), "Testing Decisions" (the hand check); ADR-0052). The spike's scene on `prototype/fenrir-physics-ragdolls-spike` is the starting point, rewritten against the built package.

- **Dependencies and assets:** the PhysicsDemo links FenrirSkeleton, FenrirAnimation and FenrirRagdoll (and their extensions with scripting); the Quaternius human and its idle and walk clips are added to its fetch list under `FENRIR_FETCH_DEMO_ASSETS` with attribution; the scene reports the scaffold's skipped line without the assets, so CI without assets passes.
- **The scene,** "Ragdolls (C++)", after the vehicle scenes: the human walking a 6 m lane on a capsule `CharacterBody`, both in one collision group, the root bone pinned at its rest translation in a hosted `Animate` item after the sampler (or an in-place clip), the ragdoll built from `DescribeHumanoid` and following; Space drops it (`Simulating`, every weight 1, a 120 N s shove on the chest, the character disabled); A toggles the left arm limp and blends it back over 1 s; K powers it, switching the clip to idle, with 1 and 2 stepping `strength` by 0.1; G gets up (`Following`, weights 1 → 0 over 1.5 s, the character teleported under the skeleton entity and re-enabled); H casts a ray from the camera and logs the bone hit through `RagdollBody::bone`; 3 drops a bystander human in a group of its own into the lane; B drops a 5 kg crate on the head; L logs state; three crates and a low wall; readouts in entity names (mode, strength, body count, awake, hips weight, hips body position, skeleton entity position, the character's enabled and ground) shown as the scaffold's screen text; the scaffold's follow camera; P autopilots A, A, Space, K, G, 3, B, L over about 17 s.
- **F2** gains "ragdolls" in its cycle. A `CheckSceneLog.cmake` entry lists the scene's key and autopilot lines.
- **README:** the scene in the other families' format, with the hand-check list: hit boxes following under F2 with no lag; a limp fall without popping or flying; the arm hanging without stretching; the powered pose holding and recovering from a crate; getting up where it lies with no jump; the bone logged; the bystander colliding with the first ragdoll and neither touching its own character; identical readouts over two smoke runs; regressions to watch: the walk's root translation sliding the hit boxes, and powered legs stepping while a locomotion clip plays.

**Blocked by:** 05 (Edges, determinism and ragdoll debug lines).

**Touches:** `examples/PhysicsDemo/`, `examples/assets/`

**Status:** ready-for-agent

- [ ] `FENRIR_DEMO_SCENE="Ragdolls (C++)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and every key with no errors, Debug and Release; the CTest scene-log check passes; without the assets the scene reports skipped; two smoke runs print identical readouts.
- [ ] The scene is reached by PageDown after the vehicle scenes and F1 lists its keys.
- [ ] The README documents the scene and its hand-check list.
- [ ] Checked by hand: the hit boxes follow the walk under F2; Space, A, K (with 1 and 2), G, H, 3, B and L do what the ticket says; the bystander and the first ragdoll collide and ignore their own characters.
