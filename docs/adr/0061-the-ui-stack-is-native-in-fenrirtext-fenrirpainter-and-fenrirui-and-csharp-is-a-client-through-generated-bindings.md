---
status: proposed
---

# The UI stack is native in FenrirText, FenrirPainter and FenrirUI, and C# is a client through generated bindings

Game UIs are to be built on FenrirEngine with or without its C# scripting (ADR-0007, ADR-0009) and without a Razor-style syntax, with that syntax as an optional convenience as s&box's is. Research on 2026-11-10 (`.scratch/fenrir-ui/research/sbox-ui-stack.md`, `engine-ui-authoring-models.md`) found two shapes: s&box keeps the whole tree, cascade, layout, text and batcher in C# and native sees only buffers, so there is no UI without the managed runtime; Noesis, Godot, Unity's native core and RmlUi keep layout, style resolution, painting and text native and expose a tree and a binding surface. This ADR picks the second shape and the packages it lives in. Settled in a grilling session on 2026-11-10.

## Native, in three packages

- **FenrirText**: fonts, shaping, paragraph layout and glyph atlases (ADR-0062). Its own package because labels in the world, debug tooling and the painter all want text without a painter.
- **FenrirPainter**: paths, paints, shapes, transforms, clips, offscreen groups and text drawing, recording into `DrawList2D` (ADR-0060). Depends on FenrirText and FenrirDraw2D.
- **FenrirUI**: the element tree, layout, style, markup and stylesheet loading, input routing through FenrirInput's routers (ADR-0027), focus and the built-in controls. Depends on FenrirPainter.

Dependencies point down only, and none of the three knows the renderer or the scripting. The engine's UI is complete in C++: a game with scripting off builds and runs the same UI.

## C# is a client

Each package gets its C# contract the way every engine extension does (ADR-0042): a generated binding from its TOML description, with the painter as a **facade** (a static class with one native per draw call, the shape of SkiaSharp's `SKCanvas`, since a painter's answers depend on calls), elements as **wrapper handles** with natives for their properties, classes, children and events, and stylesheets and markup loaded by path. Native code never calls into a C# object per element, since scripting hosts C# only in stages (ADR-0039): custom drawing from C# is a C# system in a hosted `Paint` stage of FenrirUI, painting through the facade into the element it owns. UI events reach C# as engine events (ADR-0012), one queue read by both languages.

*Amended 2026-11-12 (FenrirUI ticket 19):* outside FenrirUI, a C# system gets its painter from the overlay, by window and layer, for the frame (`Painter.Overlay`): the painter's scripting extension owns the draw list it records into and submits it to the `Overlay` resource when the calling system returns, so C# holds no draw list and calls no submit, and the extension links FenrirRenderer for the header-only `Overlay` while FenrirPainter stays renderer-free. Every C# surface of FenrirText and FenrirPainter is generated; the painter's extension draws the text extension's paragraphs by sharing its state.

**A Razor-style syntax is a separate, later effort**: a C# source generator over the same element API, as s&box's Razor is a convenience over its Panel API. Nothing in this stack assumes it.

## Considered

- **s&box's shape, UI in C#.** It is the one the user named as the feel to match, and it is why the authoring layer is designed to make Razor an optional convenience; but it makes C# mandatory, which the engine's scripting is not.
- **Tree native, custom elements subclassed in C#.** Needs native-to-managed calls per element per frame, which scripting does not offer, and a second object model to keep in step. A C# custom element composes built-ins and paints from a stage instead.
- **One package.** The painter without the tree is what a HUD drawn by code, a debug tool or a world-space label wants, and text without either is what the renderer's own screen text could use; three packages keep each usable alone.
