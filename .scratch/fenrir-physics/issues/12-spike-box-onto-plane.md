# Spike: a box dropping onto a plane through the proposed seam

Type: prototype
Status: resolved
Blocked by: 03, 04, 05, 13

## Question

Do the decided seam, ECS model and stepping actually work together? Build a throwaway spike in the DemoApp: a static plane and a dynamic box, made through FenrirPhysics's proposed public interface with Jolt behind it, stepped in `Tick` and drawn through the existing `Transform` rendering. React to it: does the public interface read well, does the sync feel right, does anything about Jolt leak through? Keep it on a `prototype/fenrir-physics-spike` branch and link it here.

Also confirm the build settings from "How does Jolt join the build?": FenrirPhysics with Jolt builds in every preset (Windows Clang and MSVC, debug and release; Linux) with those overrides, and note any line `libs/CMakeLists.txt` needed.

## Answer

**Verdict: the seam, ECS model and stepping work together; nothing about Jolt leaks.** The user ran the DemoApp scene: boxes fall and stack on a static ground, a kinematic paddle moved in `Tick` shoves them, and editing a box's transform in the inspector teleports it.

Prototype (primary source, never merged): branch `prototype/fenrir-physics-spike` (`32a2ad9`, plus `0bd1ff8` colouring the demo bodies). The thin slice: `Physics::Install`, `PhysicsWorld` as a scene resource, `RigidBody` and `Collider` (box and sphere in the tagged union), a private `src/Backend/Jolt/`, the `PostTick` pipeline (component watches, diff, step, write-back through `SetWorldTransform`), crude `PreTick`/`PostTick`, a tick cap, scene resources, `ComponentWatch` (native only) and world-transform functions. Also a headless check, `FenrirPhysicsSpikeCheck`.

**Evidence:**

- **Seam:** `JoltBackend.cpp` is the only file including Jolt; Jolt's include path reaches only FenrirPhysics's three translation units (checked in `compile_commands.json`); public headers name nothing from Jolt.
- **Build settings**, unchanged from "How does Jolt join the build?" (plus `ENABLE_INSTALL OFF`, harmless): FenrirPhysics and Jolt build in clang-x64-debug, clang-x64-release, msvc-x64-debug, msvc-x64-release, with zero warnings from Jolt's sources; `set_target_properties(Jolt PROPERTIES SYSTEM TRUE)` gives the system includes. **Not verified:** Linux (no machine) and MSVC x86 (not built).
- **Behaviour** (headless check, same results in all four presets): a box and a sphere (the sphere a child of a moved, rotated parent) fall and rest; two runs in one process are bit-identical.

**Lessons for the foundation spec (accepted by the user):**

1. A component watch records only changes after it is created; a world created when its scene already has bodies scans the existing colliders once. The watch API stays "changes from now on".
2. A kinematic body gets `MoveKinematic` every step, even when unmoved; otherwise it keeps its last velocity and drifts.
3. After write-back, the diff's copy of a body's world transform is read back through `GetWorldTransform`, not stored from the backend's result; otherwise round-trip rounding through a parent looks like a game edit and teleports the body every step.
4. Bodies rest about 0.02 into what they stand on (a 0.5 box rests at y=0.48): Jolt's penetration slop. Tests use a tolerance. The slop is **not** exposed (ADR-0010 refuses backend tuning).
5. Scale baked into the shape works; a box's convex radius is clamped for thin boxes.
6. The systems `Install` adds hold a pointer to `Physics`'s state, so the owner must outlive `App::Run`; the spec states that rule (or systems become removable).
7. **Risk, not spiked:** Jolt's jobs on the `ThreadPool` (the spike stepped single-threaded). The adapter and its no-deadlock rule ("How does physics step?") are untested; the build should test them early.

**Engine problems the spike hit (not caused by physics):**

- `ConsoleLogger` registers a process-wide spdlog logger named `"FENRIR"`, so a second `App` in one process throws (first seen as an abort with exit code 3). Affects any test that makes two apps.
- FenrirScripting does not compile under MSVC (`ScriptingExtension.hpp:145`, ambiguous `NativeFunctionTraits` partial specialization), so the MSVC presets cannot build the full DemoApp.
- The DemoApp's third-party libraries (assimp, glad, glfw, glaze, imgui, ImGuizmo, stb_image, v8) are gitignored local copies, so a fresh clone or worktree cannot build it.

