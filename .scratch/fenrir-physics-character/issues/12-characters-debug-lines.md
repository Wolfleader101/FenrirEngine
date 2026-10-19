# 12: Characters debug lines

**What to build:** Any renderer can show the shape a character really uses, where its ground is and how it moved, through a new debug-line category (spec: "FenrirPhysics: debug lines", "DemoApp" (the F2 cycle)). `DebugDraw::Characters` is included in `All` and mirrored in C#'s debug-draw enum. For each active character it draws:

- **the shape in use** as a wireframe at its step pose, in a new `Character` colour: the crouched shape while a stand-up is blocked, the shape the inner body presents to bodies and queries;
- **the ground normal** from the ground point, coloured by ground state;
- **a `movedVelocity` line** as far as the character travels in a tenth of a second, like `Velocities`.

Inactive and disabled characters draw nothing. `Colliders` still skips characters. The DemoApp's F2 cycle gains "characters" before "all".

**Blocked by:** 10 (Shape changes and crouching): the blocked shape is drawn while `shapeBlocked`.

**Status:** ready-for-agent

- [ ] Tests: with `Characters` on, a known character gives the expected line count; with it off, none; `All` includes it; `Colliders` draws no lines for a character.
- [ ] Tests: while `shapeBlocked`, the drawn shape is the crouched one; inactive and disabled characters draw nothing.
- [ ] The C# debug-draw enum has the `Characters` value matching C++.
- [ ] F2 cycles through "characters"; the lines show the shape, ground normal and moved velocity, and the larger shape under G (checked by hand).
