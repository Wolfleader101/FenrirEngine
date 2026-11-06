# 08: PhysicsDemo: the Ragdolls (C#) scene

**What to build:** the PhysicsDemo's "Ragdolls (C#)" scene, built only from C# through `Fenrir.Ragdoll`: the same human described and built from C#, a drop and get-up cycle with the weight ramp written in C#, a partial ragdoll on one arm, a powered ragdoll with strength stepping on the idle clip, a second ragdoll in another group that collides with the first and ignores its own character, and a bone-name hit log from a raycast, with readouts, an autopilot and its scene-log check (spec: "PhysicsDemo: the two ragdoll scenes"; physics map ticket "What do the ragdoll demo scenes show?").

- **The scene,** after Ragdolls (C++), present only with scripting: one C# script builds the lane, the two humans on their character bodies (root bones pinned or an in-place clip, as the C++ scene does), describes and builds both ragdolls with `Ragdoll.DescribeHumanoid` and `Ragdoll.Build`, puts each pair in its own `CollisionGroup`, and runs the keys: Space drops the first (mode, weights, `AddImpulse`), A toggles the left arm limp with a C# blend-back, K powers it on the idle clip with 1 and 2 stepping strength, G gets it up with a C# weight ramp and re-enables the character, H logs the bone a camera ray hits, 3 drops the second into the first, L logs state; readouts in names in the C++ scene's format; P autopilots the keys.
- A `CheckSceneLog.cmake` entry lists its lines; the README adds the scene with its hand-check list (as the C++ scene's, plus "C# builds the scene with no warning").

**Blocked by:** 06 (PhysicsDemo: the Ragdolls (C++) scene); 07 (Ragdolls in C#).

**Touches:** `examples/PhysicsDemo/`

**Status:** ready-for-agent

- [ ] `FENRIR_DEMO_SCENE="Ragdolls (C#)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and every key with no errors or warnings, Debug and Release; the CTest scene-log check passes; without the assets it reports skipped; without scripting the scene is not listed.
- [ ] The README documents the scene and its hand-check list.
- [ ] Checked by hand: C# builds the scene with no warning, and each key does what the C++ scene's does; the two ragdolls collide and ignore their own characters.
