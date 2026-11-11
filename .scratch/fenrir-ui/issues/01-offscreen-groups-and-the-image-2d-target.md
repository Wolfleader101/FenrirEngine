# 01: Offscreen groups in `DrawList2D` and the renderer's image 2D target

**What to build:** a 2D draw list can be drawn into an `Image` asset, and a run of its commands can be drawn into an image of its own and composited back as one (spec: "FenrirDraw2D: offscreen groups", "FenrirRenderer: the image 2D target and groups"; ADR-0060).

- **`DrawList2D` gains begin-group and end-group commands** beside the draw: a command becomes a tagged union. Begin group carries the group's bounds in local units and its pixel format (premultiplied RGBA8, the only one); end group carries the composite: an opacity, a blend mode, a filter kind with parameters (plain this ticket; blur, drop shadow and mask are ticket 05's and are declared here so the shape is settled), or "mask the following commands until end-of-mask". Groups nest to 8; an unbalanced list is a logged error and skipped.
- **The image 2D target**: the renderer's facade takes a list for an `Image` asset of a given size and encoding (sRGB or linear), draws it with `localToClip` from the image's size, a content scale of 1 and the hardware scissor, and the image is then any image asset. Submissions are cleared after the frame as the overlay's are.
- **Groups on every target**: the 2D render feature keeps a per-frame pool of transient images keyed by size and format; a group draws its enclosed commands into one, with the group's origin as their local origin, as an image target of its own, and the end-group command draws that image over the outer target through the `builtin:2d` textured path with the opacity and blend mode. An empty group draws nothing.
- `RendererStats` gains lists, commands, groups and transient image bytes per frame; the README's overlay section and FenrirDraw2D's README describe groups and the image target; the glossary already has the terms.

**Blocked by:** none

**Touches:** `packages/FenrirDraw2D/`, `packages/FenrirRenderer/src/Core/Draw2DFeature.cpp`, `packages/FenrirRenderer/src/Core/Draw2DFeature.hpp`, `packages/FenrirRenderer/src/Facade/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] FenrirDraw2D tests: a list with nested groups round-trips its commands; the union's defaults; the header still reaches no GL, SDL, window or renderer.
- [x] Null-backend tests: a list submitted to an image target produces one pass into an image of the right size and encoding; a list with two nested groups produces two transient images, three passes and two composite draws in the right order; an unbalanced list is skipped and logged once; a depth-9 group is logged and drawn flat.
- [x] GL test (skipped without a display): a group at half opacity over a solid background reads back the blended colour; an image target read back holds what was drawn.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
