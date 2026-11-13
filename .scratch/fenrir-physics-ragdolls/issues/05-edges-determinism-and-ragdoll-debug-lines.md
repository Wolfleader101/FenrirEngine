# 05: Edges, determinism and ragdoll debug lines

**What to build:** every edge the spec names behaves and is seen: inactive and skipped rules each warn once and recover, paused scenes change nothing, runs are bit-identical single- and multi-threaded, and a new `Ragdolls` debug-line category draws each body's bone frame and offset coloured by mode and powered motor targets, in C++ and the C# category enum (spec: "the components" (inactive), "debug lines", "Testing Decisions"; ADR-0053, ADR-0025).

- **Edges:** a `Ragdoll` on an entity with no ready `SkeletonPose` or with `timing == Frame` is inactive (writes nothing, warns once per transition, the warning naming `PoseTiming::Tick`), and resumes by itself; a `RagdollBody` whose bone matches nothing, whose entity has no `RigidBody`, or whose `ragdoll` is not a `Ragdoll` entity of the same scene is skipped with one warning and `state.bone = NoBone`; a skeleton asset change (rebuild) re-resolves names; a glue-owned field written by game code is overwritten next tick; destroying a world or scene with ragdolls is clean.
- **Determinism:** the same inputs give the same ragdoll, single- and multi-threaded, with physics's threading option.
- **Debug lines:** `PhysicsDebugCategories::Ragdolls` (C++ and C#, `All` includes it), drawn by FenrirRagdoll into the scene's `DebugDraw` in the `Physics` group with duration 0 after its `Update` work: each body's bone frame (small axes at the bone's simulation world matrix) and a line from the bone to the body's origin, coloured by effective mode (following grey, simulating orange, powered green); powered motor targets as small frames at the joint; nothing for inactive or skipped bodies.

**Blocked by:** 04 (Powered ragdolls and strength).

**Touches:** `packages/FenrirRagdoll/`, `packages/FenrirPhysics/include/FenrirPhysics/DebugLines.hpp`, `packages/FenrirPhysics/Managed/`

**Status:** resolved

- [x] `FenrirRagdollTests`: each inactive and skipped case warns exactly once per transition and recovers; a skeleton asset change re-resolves bones; a write to `motionType` or a following body's `Transform` is overwritten next tick; a paused scene changes nothing; destroying the scene with a live ragdoll is clean.
- [x] A fall onto boxes over 300 ticks is bit-identical in two runs, single- and multi-threaded.
- [x] Debug lines: counts and colours for a known ragdoll in each mode; motor target frames only while powered; nothing for inactive bodies; off when the category is off.
- [x] `PhysicsScriptingTests`: the C# `Ragdolls` category equals the C++ bit and `All` includes it.

**Resolved by:** branch `ragdoll/05-edges`. Every inactive and skipped rule warns once per transition and resumes by
itself (`Edges.cpp`); warnings about entities that are gone or lost their component are forgotten, so a new transition
warns again. **Decided:** removing `Ragdoll` (or destroying the skeleton entity) turns each body's joint to its parent
body off once, as the body is first skipped (motor mode `Off`, other fields left), after which the joints are game
code's: a removed ragdoll drives nothing towards targets nobody writes, and game code can still use the joints.
`Ragdolls::ClearOverrides` now works without the `Ragdoll`, while a body names the skeleton or the glue's record of
written bones is there (the record is kept while the skeleton entity lives), false only for a dead entity or one
nothing names; the entries themselves stay until cleared (ADR-0038). Both recorded as a dated amendment to ADR-0053's
lifetime rule and the spec. Names resolve every tick, so a rebuilt asset with reordered bones re-resolves at once;
writes to `motionType`, velocities, a powered motor and the followed skeleton entity are overwritten next tick; a
paused scene changes nothing in any mode; destroying the scene, the Physics owner or the glue under a live ragdoll is
clean. A partial-ragdoll determinism run joins the three existing ones. `PhysicsDebugCategories::Ragdolls = 1u << 7`
in C++ and C# (`All` includes it); FenrirRagdoll draws it in `Update` step 4 (`FenrirRagdoll/DebugLines.hpp`: each
resolved body's 10 cm bone frame and bone-to-body line in `RagdollDebugColour` Following `#BDBDBD`, Simulating
`#FF8C1A`, Powered `#2EB82E`, and each driven joint's target as 6 cm `DebugDraw::Axes` at frameA turned by the target),
skipping joints of kinds with no motor. Not done: an inactive (frame-posed or unready) ragdoll still leaves powered
motors at their last targets, as "writes nothing" says; strengths and a root drive are untouched (with the user).
