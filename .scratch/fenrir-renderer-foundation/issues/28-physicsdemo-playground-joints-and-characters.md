# 28: PhysicsDemo: Playground, Joints and Character, in C++ and C#

**What to build:** PhysicsDemo exists on the demo scaffold with its first six scenes ported faithfully from the legacy DemoApp: Playground (C++), Playground (C#), Joints (C++), Joints (C#), Character (C++), Character (C#), with F2 cycling the physics debug categories (spec: "Demo apps" (PhysicsDemo scenes, the faithful port, assets); ticket 23). The four vehicle scenes are ticket 29.

- **`examples/PhysicsDemo/`** through `fenrir_add_demo_app`, built with `FENRIR_ENABLE_PHYSICS` (the C# scenes only with scripting), each scene its own source file so ticket 29 adds the vehicle scenes without editing these.
- **Scenes, in order:** Playground (C++) (the legacy Default's box stack, paddle and triangle-mesh backpack), Playground (C#), Joints (C++), Joints (C#), Character (C++), Character (C#).
- **The port is faithful:** the same bodies, keys, autopilots and log lines. Only these change: scene switching (PageUp/PageDown through the scaffold); `DemoMesh` becomes `MeshRenderer`, with the trigger pad's tint swapping to a second shared material; debug lines go through FenrirDebugDraw (physics's `Physics` group); chase cameras become `Camera` entities; the gamepad comes from `Gamepads`, replacing `DemoGamepad`; the left-click raycast comes from the scaffold's scene-click event.
- **F2** cycles the physics debug categories through `SetDebugDraw(PhysicsDebugCategories)`.
- Shared assets from `examples/assets/`; the app's own under `examples/PhysicsDemo/assets/`.
- **README:** the scenes and keys, and the hand-check lists of physics tickets 29 and 39 (joints and characters) moved in from the legacy README.
- Smoke tests registered per backend.

**Blocked by:** 18 (FenrirPhysics draws its debug output through FenrirDebugDraw); 26 (The Fenrir.DebugDraw scripting extension).

**Touches:** `examples/PhysicsDemo/`, `examples/CMakeLists.txt`

**Status:** resolved

Hand check: each scene behaves as its legacy scene did, following the hand-check lists moved into examples/PhysicsDemo/README.md (Playground, Joints and Character, in C++ and C#), including the left-click raycast in Playground (C#), the trigger zone's tint swap and the screen-text readouts.

- [x] `FENRIR_DEMO_SMOKE` with `FENRIR_DEMO_KEYS=1` runs all six scenes on null and GL with no errors, printing the legacy scenes' log lines and running their autopilots.
- [x] Each scene's bodies, keys and log lines match the legacy scene (checked against the legacy sources).
- [x] F2 cycles the debug categories, shown through FenrirDebugDraw.
- [ ] By hand, the moved hand-check lists for Playground, Joints and Character pass in both languages, including the left-click raycast, the trigger pad's tint and the gamepad (checked by hand).
