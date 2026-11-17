# 04: A window root sets the window's cursor from its hovered element's `cursor`

**What to build:** `cursor` applied to the window, on by default for window roots, and readable for any root (spec: "The window cursor"; ADR-0065). Decided with the user on 2026-11-16: on by default, with a switch per root.

- **FenrirWindow:** `CursorShape::Move` (SDL's move cursor), documented; the window binding and its generated C# enum gain it.
- **`UIRootConfig::applyCursor`** (default true), documented as meaningful for window roots only.
- **Applying it:** after input routing each frame, the window root with `applyCursor` whose router took the pointer sets its hovered element's shape through `Window::SetCursorShape` when it differs from the last FenrirUI set on that window; with no such root over the pointer, the arrow once if FenrirUI's last shape was not the arrow, then nothing. Nothing while the window's cursor mode is not `Normal`. The mapping is the spec's (`auto` the I-beam over a text entry, the arrow elsewhere; `none` hidden).
- **`UIRoot::GetHoveredCursor()`** for any root, and on the `UIRoot` mirror in C# (the binding regenerated).
- **The default stylesheet** gives the controls their cursors as the spec lists.
- **Docs:** the `Cursor` enum's comment ("FenrirUI does not set the window's cursor") and the README's `cursor` entry say what is applied and when; the glossary's *UI root* needs nothing.
- **UIDemo:** the menu's buttons show the hand and its text entry the I-beam, from the default stylesheet; the room's system applies the light terminal's `GetHoveredCursor()` while it gives the terminal a ray, and the arrow when it stops. The README's world roots and hand-check sections say so.

**Blocked by:** 01 (One CaptureTarget and test cleanups).

**Touches:** `packages/FenrirWindow/` (`WindowTypes.hpp`, the SDL cursor mapping, its binding), `packages/FenrirUI/src/` (`InputRouting.cpp`, `UIRoot.cpp`, `UI.cpp`), `packages/FenrirUI/include/FenrirUI/` (`UIRoot.hpp`, `Style.hpp`), `packages/FenrirUI/Resources/default.css`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/README.md`, `examples/UIDemo/src/World.cpp`, `examples/UIDemo/README.md`

**Status:** resolved

- [x] Over a button the window's shape is the hand, over a text entry the I-beam, over a `cursor: ew-resize` panel the east-west resize, over `cursor: none` hidden.
- [x] Moving between two buttons sets the shape once; leaving every root sets the arrow once; a `pointer-events: none` element under the pointer sets nothing.
- [x] With the cursor mode `Captured`, nothing is set; with `applyCursor` off, nothing is set.
- [x] Two window roots: the one with the higher input priority under the pointer decides.
- [x] An image root and a world root never set the window's shape; a world root's `GetHoveredCursor()` gives its hovered button's `Pointer` once its ray is on it.
- [x] The UI scripting tests read `GetHoveredCursor` from C#; FenrirWindow's tests set `Move`.

Hand check: in UIDemo the hand shows over the menu's buttons and the terminal's, the I-beam over the name entry, and the arrow comes back over the room.
