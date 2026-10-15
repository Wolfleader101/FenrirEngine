# What does debug rendering data look like?

Type: grilling
Status: resolved
Blocked by: 01, 03

## Question

Physics produces debug rendering data as plain data that any renderer may draw, with neither package depending on the other. What is it?

- Its shape: lines, triangles, wire shapes, text? Colours, per-category toggles (shapes, contacts, body velocities, broad-phase bounds, sleeping state)?
- Generated through Jolt's `DebugRenderer`, or by FenrirPhysics from its own body and shape data (which would survive a backend swap)?
- When it is produced (after each step, on demand), where it lives, and how a renderer reads it.
- Its cost when switched off, and whether it is compiled out of release builds.
- How it supports testing (e.g. asserting on produced shapes) as well as viewing.

## Answer

- **FenrirPhysics generates it from what it already knows** (each body's `CollisionShape` and world pose, motion type, trigger flag, buffered contacts, and bounds and sleep state asked of the backend privately), not through Jolt's `DebugRenderer`. It looks the same and asserts the same whatever the backend, works in every CMake configuration, and avoids the debug-define ABI hazard and the process-wide renderer instance. Lost: views of backend internals (broad-phase trees, constraint gizmos), which could later come privately from Jolt's renderer in Jolt-specific builds.
- **Lines only:** `DebugLine { Vec3 from; Vec3 to; std::uint32_t colour; }`; no triangles or text. Categories as bit flags:
  - `Colliders`: wireframes, coloured by motion type, dimmed when sleeping, a distinct colour for triggers;
  - `Contacts`: point plus a short normal line;
  - `Velocities`: a line from each body's centre;
  - `Bounds`: each body's bounding box.
  Colours are fixed defaults, not configurable in the foundation.
- **Produced after each step, on request:** off by default; enabled per world (`world.SetDebugDraw(DebugDraw::Colliders | DebugDraw::Contacts)`). After each step the enabled categories are regenerated into a buffer the world owns; `world.DebugLines()` returns a `std::span<const DebugLine>` valid until the next step. Off costs one flag check per step. Compiled into every build (no compile-out switch in the foundation), since tests in optimised builds need it.
- **Testing:** `PhysicsWorld` gains read-only state queries `IsSleeping(entity)` and `Bounds(entity)` (the sleep-hiding promises of "How do contact and trigger events reach game code?" need the first). Tests on debug data check what suits lines: categories switch on, known shapes give the expected line counts, lines fall inside expected bounds.
- **DemoApp:** draws `world.DebugLines()` in one `GL_LINES` pass (reviving the commented-out `DrawAABB` path), with a key cycling the categories, in the physics demo scene. C# access to the toggle belongs to "What does the physics scripting extension give C#?".
