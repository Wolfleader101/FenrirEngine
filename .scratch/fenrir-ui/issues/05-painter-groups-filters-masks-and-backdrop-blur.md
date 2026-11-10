# 05: Painter groups, filters, masks and backdrop blur

**What to build:** a run of painter draws can fade, blur, cast a drop shadow or mask what follows as one, and a panel can blur what is behind it (spec: "FenrirRenderer: the image 2D target and groups", "FenrirPainter" state; ADR-0060's gap 3).

- **Painter**: `BeginGroup(bounds, opacity, blend, filter)` and `EndGroup`, `BeginMask`/`EndMask`, and `BackdropBlur(rect, radius)`, recorded as ticket 01's commands with the painter's composite kinds.
- **Renderer**: the composite kinds the 2D feature realises with the painter's shader variants: `blur(r)` as a separable two-pass Gaussian at the group's resolution, at half resolution past a radius threshold, through one more pooled image; `drop-shadow` as the group's alpha blurred and tinted, drawn offset before the group; a mask multiplies the following commands by the group's alpha through a texture slot until end-of-mask; backdrop blur copies the target under the rectangle into a pooled image, blurs it and draws it back (the window overlay reads its own target; an image target draws to a copy). A switch on the renderer's 2D config turns groups off: content draws flat and filters are ignored.

**Blocked by:** 01, 03

**Touches:** `packages/FenrirPainter/`, `packages/FenrirRenderer/src/Core/Draw2DFeature.cpp`, `packages/FenrirRenderer/src/Core/Draw2DFeature.hpp`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] Draw-list tests: begin and end groups are balanced and nested as recorded; a mask brackets the right commands; a backdrop blur records the rectangle and radius.
- [ ] Null-backend tests: a blurred group produces two blur passes and one more transient image; a drop shadow draws before its group; a masked run binds the mask image in the expected slot; backdrop blur on the overlay copies the target region first; with groups off no transient image is made.
- [ ] GL test (skipped without a display): a half-opacity group over a solid colour, a blurred disc's edge profile, and a masked gradient read back as expected within a tolerance.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
