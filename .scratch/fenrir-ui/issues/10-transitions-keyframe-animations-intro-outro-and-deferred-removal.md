# 10: Transitions, keyframe animations, `:intro`/`:outro` and deferred removal

**What to build:** property changes animate, elements animate in on creation and out on removal, and removal waits for the outro (spec: "FenrirUI" property set (transition-*, animation-*), lifecycle; ADR-0064, ADR-0066).

- **Transitions**: `transition-property`, `-duration`, `-timing-function` (the CSS named set, `cubic-bezier()`, `steps()`), `-delay`; when a resolved property changes, the element keeps a transition entry from the old value to the new, interpolated per frame for the animatable properties (lengths, colours, opacity, transforms, shadows, radii); discrete properties switch at the end; a change mid-transition starts from the current value.
- **Animations**: `animation-name` referring to a parsed `@keyframes`, `-duration`, `-timing-function`, `-delay`, `-iteration-count` (including infinite), `-direction`, `-fill-mode`; applied over the cascade each frame.
- **Lifecycle**: `:intro` set at creation and cleared after the first completed layout; `Remove()` sets `:outro` and defers removal until the element's and its descendants' running transitions finish, then removes; `Remove(immediate)` removes at once; a removed element's id is stale after the actual removal and alive before it.
- Time comes from FenrirTime's frame delta; animations run in `PostUpdate` before the cascade's final values.

**Blocked by:** 08

**Touches:** `packages/FenrirUI/`

**Status:** resolved

- [x] Headless App tests ticking fixed deltas: a width transition reaches half way at half its duration under `linear`, and the expected fraction under `ease-in-out` and `steps(4)`; a colour transition interpolates in sRGB; a change mid-transition continues from the current value; a keyframe animation loops and reverses as its direction says and holds with `fill-mode: forwards`.
- [x] Lifecycle tests: an element has `:intro` on its first frame and not on its second; `Remove()` on an element with an opacity outro transition keeps it alive, animating, for the duration, then removes it and its id goes stale; `Remove(immediate)` removes at once; a parent's outro keeps its children alive.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
