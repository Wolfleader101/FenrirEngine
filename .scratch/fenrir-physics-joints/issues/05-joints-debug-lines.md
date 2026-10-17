# 05: Joints debug lines

**What to build:** Any renderer can show joint frames through a new debug-line category (spec: "FenrirPhysics: debug lines", "DemoApp: scene switching"). `DebugDraw::Joints` is included in `All` and mirrored in C#'s debug-draw enum. For each joint it draws both frames as axes: `frameA` long (red, green, blue), `frameB` short (pink, yellow, cyan). They coincide at the rest pose and while the joint holds. The DemoApp's F2 cycle gains "joints" before "all".

Dimmed axes for disabled joints come with breaking (ticket 09) and grey axes for inactive joints with ticket 10. No limit arcs or cones.

**Blocked by:** 03 (Joints tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: with `Joints` on, a known set of joints gives the expected line count; with it off, none; `All` includes it.
- [ ] The C# debug-draw enum has the `Joints` value matching C++.
- [ ] F2 cycles through "joints"; in the Joints (C++) scene every joint's frames coincide at rest (checked by hand).
