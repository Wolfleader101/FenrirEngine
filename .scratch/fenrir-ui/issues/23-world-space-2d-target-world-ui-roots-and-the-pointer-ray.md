# 23: World-space 2D target, world UI roots and the pointer ray

**What to build:** real UI on a quad in the scene that a player aims at and clicks, with UIDemo's terminal and name plate (spec: "FenrirRenderer" world-space target, "FenrirUI" root targets, input, "UIDemo" world-space root; ADR-0060, ADR-0066).

- **Renderer**: a world-space 2D target: an entity with a component naming a UI root, its size in world units and its UI units per world unit; the renderer draws the root's list on that quad in the `Transparent` stage, sorted with the other transparents by its origin, with `localToClip` from the quad's world matrix and the view, the hardware scissor off and clips realised in the painter's shader from the vertex's clip data, linear blend space. The game owns the entity's transform.
- **FenrirUI**: a root may name a world entity as its target; the game gives the root a pointer ray each frame (`SetPointerRay`) and a pressed state, or none; the root intersects its quad, converts the hit to UI units and runs ticket 11's hit testing, hover, press and focus from it; no router is registered for a world root (the game decides which camera and cursor the ray comes from, ADR-0057's line); gamepad navigation works on a focused world root as on any other.
- **UIDemo**: an interactive terminal on a quad in the room with buttons and a slider that change the room's light colour, hit by a ray from the camera through the cursor; a name plate root above a capsule facing the camera, in SDF text; both listed in the README.

**Blocked by:** 14, 21, 22

**Touches:** `packages/FenrirRenderer/src/Core/Draw2DFeature.cpp`, `packages/FenrirRenderer/src/Core/Draw2DFeature.hpp`, `packages/FenrirRenderer/src/Facade/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirUI/`, `examples/UIDemo/`

**Status:** resolved

- [x] Null-backend tests: a world root's list draws as one transparent-stage draw with the scissor off and the quad's matrix in set 0; a root behind the camera is culled with the other transparents.
- [x] Headless App tests: a ray through the quad's centre hovers and clicks the element there; a ray that misses clears hover; a rotated quad maps the hit correctly; a world root takes gamepad navigation when focused and publishes the gamepad claim.
- [x] GL test (skipped without a display): a world quad with a rounded clip drawn at an angle reads back a clipped edge where the clip says, not at the quad's edge.

Hand check: the terminal's buttons light the room, the slider drags under the cursor from several camera angles, the name plate's text stays crisp as the camera approaches.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
