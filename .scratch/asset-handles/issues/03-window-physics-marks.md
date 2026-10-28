# 03: `returns` marks on the window's and physics's value-returning methods

**What to build:** the three remaining hand-written methods of the shape "call a native with one `out`, return the out" become `returns` marks (ticket 01), as the spec's "What stays hand-written" bounds it.

- **`packages/FenrirWindow/Scripting/src/WindowScripting.cpp`**: `Size` and `PixelSize` get `@facade Window.Size returns size` and `@facade Window.PixelSize returns size` (check the output parameter's name in the natives header). **`packages/FenrirWindow/Managed/Fenrir.Window/Window.cs`** loses both methods, keeping `Main` and the class doc, and is `partial` beside the generated `Window.cs` if it is not already.
- **`packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp`**: `GetJointState` gets `@facade PhysicsWorld.GetJointState returns state`. **`packages/FenrirPhysics/Managed/Fenrir.Physics/PhysicsWorld.cs`** loses the method; its doc paragraph moves onto the native's doc comment so the generated method carries it. `Joints.FrameFromWorld` (a defaulting overload) and `Mouse`'s properties stay as they are.
- Regenerate both stages and commit the generated parts. The window and physics .NET tests read the same values through the generated methods.

**Blocked by:** 01

**Touches:** `packages/FenrirWindow/Scripting/src/WindowScripting.cpp`, `packages/FenrirWindow/Scripting/api/`, `packages/FenrirWindow/Managed/Fenrir.Window/`, `packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp`, `packages/FenrirPhysics/Scripting/api/`, `packages/FenrirPhysics/Managed/Fenrir.Physics/`

**Status:** ready-for-agent

- [ ] `grep -n "out WindowSize\|out JointState" packages/FenrirWindow/Managed/Fenrir.Window/Window.cs packages/FenrirPhysics/Managed/Fenrir.Physics/PhysicsWorld.cs` finds nothing; both stages regenerate to a clean tree.
- [ ] `FenrirWindowScripting` (and its display test where a display exists) and `FenrirPhysicsScripting` pass under the msvc-x64-debug preset.
- [ ] `check_tickets.py` and clang-format pass.

## Comments

Filed from the asset-handles grilling (spec decision 3). Cut from ticket 01's branch; disjoint from ticket 02.
