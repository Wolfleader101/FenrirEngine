# 03: Port the physics extension onto NativeGuard

**What to build:** Every physics native becomes its one call behind the guard (spec: "NativeGuard", "Rule tests move onto it").

- **`src/PhysicsScriptingState.hpp`**: `NativeName` and `WarnOnce` go. `WorldOf(context)` and `HasBody(context, entity)` lose their `source_location` parameter and warn through a `NativeGuard` made from the context and `warnings`; `HasBody` uses `guard.Alive` for the alive part and keeps its body rule and message.
- **`src/PhysicsScripting.cpp`**: in each of the ~40 natives remove the `== nullptr` checks on `in` and `out` parameters and the `*out = T{}` lines (the registration zeroes them since ticket 02). The span natives (`RaycastAll`, `ShapeCastAll`, `Overlap`, `Penetrations`, `OverlapPoint`, `ContactsOf`) take their output span through `SpanOf`. `Push` loses its `where` parameter. The inline conversions in `MoveCast`, `Penetrations` and `ContactsOf` use `ToScript`.
- **Tests**: the no-world and no-body cases in `Tests/PhysicsScriptingTests.cpp` (a native called with no physics world returns false and warns once; a force on an entity with no body does nothing and warns once; `JointState` on an entity without a `Joint` reads zeroed) move to a new `Tests/Rules.cpp` built on `NativeCallHarness` with a `PhysicsScripting` state the test constructs, needing no fixture assembly. The .NET tests of queries, shapes, events and facades stay as they are. `Tests/CMakeLists.txt` adds FenrirScripting's `src` include for the harness.
- **`Tests/PhysicsScriptingApp.hpp`** loses nothing; the fixture's C# `Natives` class is already gone (ticket 02).

**Blocked by:** 01, 02

**Touches:** `packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp`, `packages/FenrirPhysics/Scripting/src/PhysicsScriptingState.hpp`, `packages/FenrirPhysics/Scripting/Tests/`

**Status:** ready-for-agent

- [ ] `grep -n "nullptr" src/PhysicsScripting.cpp` finds only the span and optional checks that remain by design; no `source_location` outside the state's guard calls; no `NativeName`.
- [ ] The moved rule tests pass with no `FENRIR_TEST_PHYSICS_FIXTURE` set; the remaining .NET tests pass with it.
- [ ] A warning from a physics native in a debug run reads `[physics scripting] PhysicsNative.<Method>: ...` (one test asserts the prefix through the harness's logger).
- [ ] clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the native guard grilling (spec decisions 2, 4, 6, 14). Runs side by side with tickets 04 to 07 once 01 and 02 are merged.
