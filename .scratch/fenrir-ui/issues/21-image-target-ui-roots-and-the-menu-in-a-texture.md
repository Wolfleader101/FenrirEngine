# 21: Image-target UI roots and the menu in a texture

**What to build:** a UI root draws into an image a material shows on a mesh, and UIDemo puts its menu on a screen in the room (spec: "FenrirUI" root targets, "UIDemo" texture root; ADR-0060, ADR-0065 blend spaces).

- A root may name an image target instead of a window layer: an `Image` asset the UI owns at a size the game sets, linear blend space by default, content scale 1, submitted through ticket 01's image target each frame; the image is usable as any material's texture. Input to an image root comes only from code (no router), this ticket.
- UIDemo: the pause menu's document loaded a second time into an image root, shown on a screen mesh's material, mirroring the live menu's state through the same systems.

**Blocked by:** 18

**Touches:** `packages/FenrirUI/`, `examples/UIDemo/`

**Status:** ready-for-agent

- [ ] Headless App test (null backend): an image root submits a list to an image target of its size each frame, with the painter's pipeline in the linear blend space; the image asset exists and has the root's size.

Hand check: the screen in the room shows the menu, crisp at its native size.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
