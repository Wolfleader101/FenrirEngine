# 12: Characters debug lines

**What to build:** Any renderer can show the shape a character really uses, where its ground is and how it moved, through a new debug-line category (spec: "FenrirPhysics: debug lines", "DemoApp" (the F2 cycle)). `DebugDraw::Characters` is included in `All` and mirrored in C#'s debug-draw enum. For each active character it draws:

- **the shape in use** as a wireframe at its step pose, in a new `Character` colour: the crouched shape while a stand-up is blocked, the shape the inner body presents to bodies and queries;
- **the ground normal** from the ground point, coloured by ground state;
- **a `movedVelocity` line** as far as the character travels in a tenth of a second, like `Velocities`.

Inactive and disabled characters draw nothing. `Colliders` still skips characters. The DemoApp's F2 cycle gains "characters" before "all".

**Blocked by:** 10 (Shape changes and crouching): the blocked shape is drawn while `shapeBlocked`.

**Status:** resolved, but for the DemoApp hand check, which is the user's (built test-first on branch `claude/character-debug-lines-92a7f8`; tests in `Tests/CharacterDebugLines.cpp`, tag `[characters]`). Where the build settled something the ticket left open:

- **Colours:** `DebugColour::Character` (near white, 0xF2F2F2), `Ground` (mint, 0x33E6B3) for a normal on walkable ground and `SteepGround` (violet, 0xB366FF) on ground too steep. The moved-velocity line is `Velocity` yellow, as a body's. A character in the air or only touching draws no normal.
- **Lengths:** the ground normal is 0.5 m. The moved-velocity line starts at the character's centre (its Transform) and is left out when `movedVelocity` is zero, as `Velocities` leaves out a body at rest.
- **Hull edges:** characters share the colliders' per-mesh edge cache, so a hull character asks the backend for its edges once while drawn.

- [x] Tests: with `Characters` on, a known character gives the expected line count; with it off, none; `All` includes it; `Colliders` draws no lines for a character.
- [x] Tests: while `shapeBlocked`, the drawn shape is the crouched one; inactive and disabled characters draw nothing.
- [x] The C# debug-draw enum has the `Characters` value matching C++.
- [ ] F2 cycles through "characters"; the lines show the shape, ground normal and moved velocity, and the larger shape under G (checked by hand).
