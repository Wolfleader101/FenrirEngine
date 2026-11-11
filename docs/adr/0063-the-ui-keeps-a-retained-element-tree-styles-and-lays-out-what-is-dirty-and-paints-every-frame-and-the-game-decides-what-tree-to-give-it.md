---
status: proposed
---

# The UI keeps a retained element tree, styles and lays out what is dirty, paints every frame, and the game decides what tree to give it

Two questions shape a UI framework's cost model and its line with the game. Retained or immediate: Dear ImGui rebuilds everything every frame; Unity and Godot cache each element's draw data and invalidate; s&box retains the tree and the layout but regenerates drawing every frame ("there is no dirty state to mark"). And where the engine stops: ADR-0057 says the renderer decides, each frame, about what it was given, and the game decides what to give it, and asks that every technique sit behind a switch. This ADR applies both to FenrirUI. Settled in a grilling session on 2026-11-10.

## Retained tree, dirty style and layout, immediate painting

- **The element tree is retained.** Elements are created, parented, given classes and properties, and live until removed; markup loads into the same tree code builds.
- **Style resolution and layout run for dirty subtrees only.** A changed class, property, stylesheet or child list marks the path to the root; a clean root means nothing to do.
- **The draw list is rebuilt every frame** from the laid-out tree, through a painter that records into one `DrawList2D` per UI root and is thrown away. ADR-0027 copies every draw list into per-frame ring buffers anyway, so caching a list would save only the CPU tessellation, and a UI that animates (transitions, hover, scrolling) dirties most of what it would cache. The painter itself is immediate: it has no state across frames.
- **The cost model the spec states:** per frame, painting is linear in visible elements, style and layout in dirty elements, atlas uploads in glyphs first seen. Per-element draw caching with invalidation can be added later behind the same tree without changing its API, so it is not decided now.

## Where the line is

**The UI decides, each frame, about the tree it was given; the game decides what tree to give it.** FenrirUI owns what is the same for every game and that no game does better: layout, the style cascade, painting, text, hit testing, focus traversal, input capture, and the mechanics of its built-in controls (scrolling, text editing, a slider's drag). The game owns what depends on it: which screens exist and when they show, how they are navigated (there is no menu stack, screen manager or navigation history in the engine), the data behind them and when it changes, the theme (the engine ships one plain default stylesheet, which the game replaces), and anything about "the UI consumed this input" beyond the router rule of ADR-0027. Each technique is a switch: anti-aliasing mode, a font's SDF mode, offscreen groups, batching, a root's scale rule.

## Considered

- **Immediate mode throughout**, as ImGui. ImGui's own FAQ says it is not designed for game UI; a stylesheet, transitions, focus and text editing need identity across frames.
- **Cached draw data per element** (Unity, Godot). Lower CPU on a static screen, a dirty-tracking surface that every property has to honour, and the renderer copies the result each frame regardless; left as a later addition.
- **A screen manager in the engine.** Every game has one and no two agree on it: a stack, a state machine, scenes, tabs; it is the shape of the game.
