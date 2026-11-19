# 05: Edges, determinism and ragdoll debug lines

**What to build:** every edge the spec names behaves and is seen: inactive and skipped rules each warn once and recover, paused scenes change nothing, runs are bit-identical single- and multi-threaded, and a new `Ragdolls` debug-line category draws each body's bone frame and offset coloured by mode and powered motor targets, in C++ and the C# category enum (spec: "the components" (inactive), "debug lines", "Testing Decisions"; ADR-0053, ADR-0025).

- **Edges:** a `Ragdoll` on an entity with no ready `SkeletonPose` or with `timing == Frame` is inactive (writes nothing, warns once per transition, the warning naming `PoseTiming::Tick`), and resumes by itself; a `RagdollBody` whose bone matches nothing, whose entity has no `RigidBody`, or whose `ragdoll` is not a `Ragdoll` entity of the same scene is skipped with one warning and `state.bone = NoBone`; a skeleton asset change (rebuild) re-resolves names; a glue-owned field written by game code is overwritten next tick; destroying a world or scene with ragdolls is clean.
- **Determinism:** the same inputs give the same ragdoll, single- and multi-threaded, with physics's threading option.
- **Debug lines:** `PhysicsDebugCategories::Ragdolls` (C++ and C#, `All` includes it), drawn by FenrirRagdoll into the scene's `DebugDraw` in the `Physics` group with duration 0 after its `Update` work: each body's bone frame (small axes at the bone's simulation world matrix) and a line from the bone to the body's origin, coloured by effective mode (following grey, simulating orange, powered green); powered motor targets as small frames at the joint; nothing for inactive or skipped bodies.

**Blocked by:** 04 (Powered ragdolls and strength).

**Touches:** `packages/FenrirRagdoll/`, `packages/FenrirPhysics/include/FenrirPhysics/DebugLines.hpp`, `packages/FenrirPhysics/Managed/`

**Status:** ready-for-agent

- [ ] `FenrirRagdollTests`: each inactive and skipped case warns exactly once per transition and recovers; a skeleton asset change re-resolves bones; a write to `motionType` or a following body's `Transform` is overwritten next tick; a paused scene changes nothing; destroying the scene with a live ragdoll is clean.
- [ ] A fall onto boxes over 300 ticks is bit-identical in two runs, single- and multi-threaded.
- [ ] Debug lines: counts and colours for a known ragdoll in each mode; motor target frames only while powered; nothing for inactive bodies; off when the category is off.
- [ ] `PhysicsScriptingTests`: the C# `Ragdolls` category equals the C++ bit and `All` includes it.
