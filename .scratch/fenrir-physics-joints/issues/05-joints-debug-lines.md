# 05: Joints debug lines

**What to build:** Any renderer can show joint frames through a new debug-line category (spec: "FenrirPhysics: debug lines", "DemoApp: scene switching"). `DebugDraw::Joints` is included in `All` and mirrored in C#'s debug-draw enum. For each joint it draws both frames as axes: `frameA` long (red, green, blue), `frameB` short (pink, yellow, cyan). They coincide at the rest pose and while the joint holds. The DemoApp's F2 cycle gains "joints" before "all".

Dimmed axes for disabled joints come with breaking (ticket 09) and grey axes for inactive joints with ticket 10. No limit arcs or cones.

**Blocked by:** 03 (Joints tracer bullet).

**Status:** resolved

- [x] Tests: with `Joints` on, a known set of joints gives the expected line count; with it off, none; `All` includes it.
- [x] The C# debug-draw enum has the `Joints` value matching C++.
- [ ] F2 cycles through "joints"; in the Joints (C++) scene every joint's frames coincide at rest (checked by hand).

## Answer

Built as specified. `DebugDraw::Joints` is `1 << 4`, in `All`; C#'s `DebugDraw.Joints` has the same value. The six axis colours are `DebugColour::JointAX` … `JointBZ`. frameA's axes are 0.5 long, frameB's 0.25.

- **Where:** `PhysicsWorld::Impl::DrawJoints` in `src/DebugLines.cpp`, called from `DrawDebugLines` after each step. It draws each joint from what physics last applied (`JointRecord::applied`, with the body scales the constraint was made with) at its bodies' step poses, so the lines match the constraint, not a `Joint` edited since. A frame's position follows its body's scale and its rotation does not; the world end's frame is drawn as given, in world space.
- **Every kind draws the same two frames**, so the kinds ticket 04 adds draw with no change here. Shapes of a kind's own (limit arcs, cones) would go in `DrawJoints`, by kind; none yet.
- **Not drawn yet:** a joint with no constraint (naming no pair of made bodies). Ticket 10 draws inactive joints' surviving frames in grey, and ticket 09 dims a disabled joint's axes; until then a disabled joint draws as an enabled one.
- **Tests:** `FenrirPhysicsTests` `[debug][joints]`: a hinge to the world (its frame turned so X points up) and a fixed joint to the corner of a post stretched to twice its width give twelve lines, two of each axis colour; at each anchor both frames' axes start at the anchor and point the same way, frameA's longer; the hinge's X axis is drawn pointing up. With every other category on there are no joint lines; with `All` there are twelve. `FenrirPhysicsScripting`: C#'s `Joints` and `All` set exactly C++'s.
- **DemoApp:** F2 cycles off, colliders, contacts, velocities, bounds, joints, all. `FENRIR_DEMO_SCENE="Joints (C++)"` starts in the scene with no warnings or errors.
- **Checked by running:** clang debug and release, every suite. MSVC x64 debug and release (scripting off), `FenrirPhysicsTests`. No build adds a warning in the code this ticket touched.
- **Found, not fixed:** MSVC x64 does not build the DemoApp on this base: `static constexpr std::array<Fenrir::Math::Vec3, …>` in `JointsScene.hpp` (chain colours) and `main.cpp` (`boxColours`) is not a constant expression there, because glm's `vec3(x, y, z)` constructor is not `constexpr` under MSVC. Both were there before this ticket.
- **Left for the hand check:** in the Joints (C++) scene, F2 to "joints": every joint's long and short axes coincide at rest.
