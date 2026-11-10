Status: ready-for-agent

# FenrirUI: a 2D painter over FenrirDraw2D, a text stack, and a retained element tree with markup and stylesheets, with UIDemo

Origin: the user wants a fully functioning 2D painter on which game UIs are built, and an authoring layer above it that feels like s&box's panels but needs neither the C# scripting nor a Razor-style syntax, with that syntax as a later optional convenience. Three research notes on 2026-11-10 (`research/sbox-ui-stack.md`, `research/engine-ui-authoring-models.md`, `research/painter-api-designs.md`) settled the facts; a grilling session the same day settled the decisions, recorded in these ADRs, which this spec cites rather than restates:

| ADR | Decision |
|---|---|
| 0060 | The painter records into `DrawList2D` with analytic shapes and tessellated paths in one fat vertex, and `DrawList2D` gains offscreen groups |
| 0061 | The UI stack is native in FenrirText, FenrirPainter and FenrirUI, and C# is a client through generated bindings |
| 0062 | Text shapes with HarfBuzz and rasterises with FreeType into dynamic atlases, hinted bitmaps by default and SDF per font |
| 0063 | The UI keeps a retained element tree, styles and lays out what is dirty, paints every frame, and the game decides what tree to give it |
| 0064 | UI is authored as a code tree that HTML-flavoured markup and a CSS subset load into, and laid out by Yoga flexbox behind a seam |
| 0065 | UI data is pushed by systems, UI events are engine events, pointer input is opt-in per element, and UI on the window blends in gamma |
| 0066 | UI elements are FenrirUI's own tree addressed by versioned ids, not entities, and removal waits for outro transitions |

They build on ADR-0027 (FenrirDraw2D, the 2D target, input routers), ADR-0016 (a UI reaches the renderer only through a public interface), ADR-0057 (the renderer decides per frame about what it was given, the game decides what to give it) and the scripting ADRs 0007, 0009, 0012, 0029, 0039 and 0042. ADR-0060 amends ADR-0027.

## Problem Statement

The engine can draw 2D: `DrawList2D` is plain data the renderer draws into a window's overlay, and ImGui's glue fills it for the demos. But nothing in the engine produces a game's UI from it. There is no painter (a rounded rectangle, a gradient, a stroked path or a line of text is vertices a game would have to tessellate itself), no text beyond the renderer's one debug font, and no notion of an element, a layout, a style, focus or a button. A game developer, or a web or desktop developer coming to the engine, has to build a health bar from triangles. ImGui's own FAQ says it is not for game UI, and it needs C++ at that. With scripting on, C# can fill vertices through the mirrored draw list and nothing more.

The user wants what a modern software developer expects of a game UI: a drawing API that does the hard parts (anti-aliased shapes, gradients, clipping, text in any script), a tree of elements laid out by flexbox and styled by a stylesheet in files that hot-reload, controls that work with mouse, keyboard and gamepad, enter and exit animations, UI on the screen, in a texture and in the world, all of it usable from C++ alone, and all of it reachable from C# when scripting is present, without a Razor-style syntax being required.

## Solution

Three native packages over the extended FenrirDraw2D, each usable alone, with dependencies pointing down only (ADR-0061):

- **FenrirText** loads fonts, shapes runs with HarfBuzz, orders bidi runs with SheenBidi, breaks lines with libunibreak, lays out paragraphs, and rasterises glyphs with FreeType into dynamic atlases, hinted bitmaps by default and SDF per font (ADR-0062).
- **FenrirPainter** is a painter that records paths, shapes, images, clips, offscreen groups and text into one `DrawList2D` with its own shader and fat vertex: rounded rectangles, borders, shadows, circles, arcs and gradients evaluated analytically per pixel, arbitrary paths and strokes tessellated on the CPU with an anti-aliasing fringe (ADR-0060).
- **FenrirUI** keeps UI roots of elements, resolves styles from stylesheets, lays out with Yoga behind a seam, paints every frame through the painter, routes input through FenrirInput's routers, moves focus by pointer, keyboard and gamepad, ships a plain default stylesheet and a small control set, loads markup documents and stylesheets from files with hot reload, and sends every UI event as an engine event (ADR-0063 to 0066).

**FenrirDraw2D** gains offscreen groups, and **FenrirRenderer**'s 2D render feature gains the image 2D target that realises them and that a game samples as an image, and, last, the world-space 2D target (ADR-0060). **C#** gets each package's contract through a generated binding: the painter as a facade, elements as handles, UI events as shared events, a hosted paint stage for custom drawing (ADR-0061). **UIDemo** shows a HUD, a pause menu, the menu in a texture on a quad, and an interactive world-space UI root.

## User Stories

### Painting

1. As a C++ game developer, I want to fill and stroke paths, rectangles, rounded rectangles, circles, arcs and polygons with a solid colour, a gradient or an image, so that I can draw a HUD element without tessellating it myself.
2. As a game developer, I want anti-aliased edges on every shape by default, so that UI looks finished at any resolution.
3. As a game developer, I want strokes with a width, cap, join, miter limit and dash pattern, so that outlines, dividers and dashed indicators draw as a designer specifies them.
4. As a game developer, I want a save/restore transform stack with translate, scale and rotate, so that I can draw a compass or a rotating icon by rotating the painter, not the geometry.
5. As a game developer, I want to clip to a rectangle or a rounded rectangle, and to an arbitrary path through a mask, so that content inside a rounded panel stays inside it.
6. As a game developer, I want to draw an image into a rectangle with a sampler, with nine-slice stretching, so that bordered panels scale without distorting their corners.
7. As a game developer, I want to draw a box shadow with offset, blur and spread, so that panels lift off the scene as they do on the web.
8. As a game developer, I want an opacity group, a blur filter and a drop shadow over a group of draws, so that a whole panel fades, blurs or casts a shadow as one.
9. As a game developer, I want a backdrop blur behind a panel, so that a frosted pause menu reads over a busy scene.
10. As a game developer, I want to draw laid-out text with the same painter, so that labels and shapes batch together and share clips and transforms.
11. As a game developer, I want the painter's output to be an ordinary `DrawList2D`, so that I submit it to the overlay, an image or a world quad exactly as today's lists are submitted.
12. As a tool developer, I want a painter with no state across frames and no GPU dependency, so that I can paint from any system and test the result without a window.

### Text

13. As a game developer, I want to load any TrueType or OpenType font and draw text in it at any size, so that the UI uses the game's typography.
14. As a player of a localised game, I want Arabic, Hebrew, Devanagari, Thai and CJK text to shape and order correctly, so that my language is readable.
15. As a game developer, I want a paragraph laid out to a width with alignment, line height, letter spacing, wrapping and an ellipsis, so that descriptions fit their boxes.
16. As a game developer, I want runs of mixed style in one paragraph, so that a tooltip can bold a keyword.
17. As a game developer, I want small text crisp through hinting, so that menus are readable at 1080p.
18. As a game developer, I want a font to opt into a distance-field mode, so that world-space and animated text scales without blur or re-rasterising.
19. As a game developer, I want to ask where a character is in a laid-out paragraph and which character is under a point, so that carets and selections are right.
20. As a game developer, I want glyphs rasterised on first use into an atlas that grows, so that I never list glyph ranges.
21. As a game developer, I want a fallback chain and an embedded default face, so that text never disappears when a glyph or font is missing.

### Elements, layout and style

22. As a game developer, I want to build a tree of elements from C++ with no files, so that a UI can be made entirely in code.
23. As a UI developer, I want to write the structure of a screen in an HTML-flavoured markup document with ids, classes and includes, so that a screen is a file I can read, diff and reuse.
24. As a UI developer, I want to style elements with a CSS subset using type, class, id, descendant and child selectors and pseudo-classes, so that what I know from the web transfers.
25. As a UI developer, I want CSS custom properties and nesting, so that a theme is a handful of variables and rules stay close to their parents.
26. As a UI developer, I want flexbox layout with percentages, gaps, absolute positioning and min and max sizes, so that a layout adapts to any screen without hand-placed coordinates.
27. As a UI developer, I want a UI authored at 1080p to keep its proportions at every resolution, so that I design once.
28. As a UI developer, I want to opt a root into following the operating system's content scale instead, so that a desktop tool's UI matches the desktop.
29. As a UI developer, I want transitions on property changes and keyframe animations, so that hover states and alerts move.
30. As a UI developer, I want `:intro` and `:outro` pseudo-classes with removal deferred until the outro finishes, so that screens animate in and out with one rule.
31. As a UI developer, I want backgrounds with colours, gradients and images, borders with per-side widths and per-corner radii, box shadows, opacity, transforms with an origin, overflow clipping and scrolling, and blur and drop-shadow filters, so that a designer's mock-up is reproducible.
32. As a UI developer, I want text properties (family, size, weight, style, colour, alignment, line height, letter spacing, shadow, white-space, ellipsis) on any element with text, so that typography is styled, not coded.
33. As a UI developer, I want `@font-face` in a stylesheet, so that fonts are named where they are used.
34. As a UI developer, I want to edit a stylesheet or a markup document and see the running game update, so that iteration is seconds, not a rebuild.
35. As a UI developer, I want load errors that name the file and line for an unknown tag, attribute or property, so that a typo is found at once rather than silently ignored.
36. As a UI developer, I want a plain default stylesheet that makes every control usable, so that a new project has a working UI on day one and replaces the look later.
37. As a game developer, I want several UI roots at once (a HUD, a menu, a debug panel), ordered, so that screens compose.
38. As a game developer, I want an element's laid-out rectangle readable from code, so that I can place a world marker or a tooltip against it.

### Data and events

39. As a C++ or C# game developer, I want to find an element by id or class and set its text, classes, visibility or inline style from a system, so that game state reaches the UI through the same ECS systems as everything else.
40. As a game developer, I want every UI event (click, press, release, hover, change, focus, blur, submit, scroll) as an engine event naming the element, so that a system reads clicks the way it reads any event.
41. As a C++ game developer, I want to attach a listener to an element that bubbles to its ancestors with stop-propagation, so that a panel can handle clicks for all of its buttons.
42. As a game developer, I want controls to expose their value (checked, slider position, entered text) through the element, so that reading the UI is as simple as writing it.

### Input and focus

43. As a game developer, I want a HUD to let every click and key through to the game by default, so that overlays never steal input.
44. As a game developer, I want a menu's root to take pointer input with one rule, so that opening a menu captures the mouse.
45. As a player, I want hover and pressed states on buttons, so that the UI responds to me.
46. As a player, I want to Tab between controls and operate them with Enter and Space, so that a menu is usable from the keyboard.
47. As a gamepad player, I want the stick or D-pad to move focus to the nearest control in that direction, with a focus ring, so that menus work on a controller.
48. As a gamepad player, I want an optional virtual cursor from the stick, so that a pointer-driven screen is usable too.
49. As a game developer, I want the UI to say which input it consumed through the existing input claims, so that the game and the OS text input behave.
50. As a player, I want scrolling with the wheel, by dragging a scrollbar and by dragging the content, so that long lists are reachable.
51. As a player, I want hit testing to honour rounded corners and clipping, so that clicks on a panel's cut corner reach what is behind.

### Controls

52. As a UI developer, I want a panel, a label, an image, a button, a checkbox, a slider, a progress bar, a scroll view and a single-line text entry, so that menus and settings screens need no custom code.
53. As a player, I want a text entry with a caret, selection by keyboard and mouse, word jumps, clipboard cut, copy and paste, Enter to submit and Escape to leave, so that naming a character feels native.
54. As a player using an input method editor, I want the OS candidate window placed at the caret, so that composing CJK text works where the OS supports it.
55. As a UI developer, I want a password-masked text entry with a maximum length, so that a login box needs no custom control.
56. As a UI developer, I want controls restyled entirely from the stylesheet, so that I never subclass to change a look.

### Custom elements

57. As a C++ game developer, I want to subclass an element with measure and paint overrides and register it under a tag name, so that a minimap or a radial menu is an element in markup like any other.
58. As a C# game developer, I want to compose built-in elements into a reusable piece and paint into an element I own from a hosted paint stage, so that custom drawing is possible from scripts without native callbacks.

### Targets and the world

59. As a game developer, I want a UI root drawn into an image asset, so that a material can show a menu on a screen in the scene or a camera's render target can sit inside the UI.
60. As a game developer, I want a UI root on a quad in the world, with pixel-exact clips and distance-field text, so that a terminal, a name plate or an in-world menu is real UI, not a texture.
61. As a game developer, I want to aim a pointer ray at a world UI and click, so that in-world panels are interactive from any camera or controller the game chooses.
62. As a game developer, I want UI on the window to blend in gamma space by default and UI in images and the world to blend linear, so that screen UI matches a designer's references and world UI matches the lit scene.

### Scripting

63. As a C# game developer, I want a painter facade with one call per draw, so that a HUD can be painted from a script.
64. As a C# game developer, I want element handles with the same properties, classes, children and queries as C++, so that a UI built in C++ and one built in C# are interchangeable.
65. As a C# game developer, I want to load markup documents and stylesheets by path, so that scripts and files work together as they do in C++.
66. As a C# game developer, I want UI events through an `EventReader`, so that clicks are read the way every other event is.
67. As an engine developer, I want every C# surface generated from the packages' API descriptions, so that C++ and C# cannot drift.

### Engine and demo

68. As an engine developer, I want each technique (anti-aliasing mode, SDF text, offscreen groups, hot reload, the scale rule, the virtual cursor) behind a switch, so that a game turns off what it does not want.
69. As an engine developer, I want no menu stack, screen manager or theme opinion in the engine, so that the shape of a game's UI stays the game's.
70. As an engine developer, I want the painter, text and UI tested without a GPU, so that CI covers them on every platform.
71. As a user of the engine, I want a UIDemo that shows a HUD, a menu, a menu in a texture and a world-space UI, with hot reload and gamepad navigation, so that I can see what the stack does and copy from it.

## Implementation Decisions

### FenrirDraw2D: offscreen groups (ADR-0060)

- `DrawList2D` gains two command kinds beside the draw: **begin group** (the group's bounds in local units, a pixel format: RGBA8 premultiplied is the only one in this version) and **end group** (how the image composites: an opacity, a blend mode, a filter with parameters, or as a mask over the commands that follow until the next end-of-mask). Groups nest to a depth of 8; a deeper group is a logged error and draws flat.
- A command is a tagged union of draw, begin group, end group; a list with an unbalanced group is a logged error and the renderer skips the list.
- The painter's **fat vertex** layout and the painter's pipeline description (its shader asset, `PremultipliedAlpha` blend mode, sRGB input colour space, blend space set per root) are FenrirPainter's, declared through the existing vertex layout description; FenrirDraw2D learns nothing about them.
- The glossary term is **offscreen group**; "layer" stays the overlay's ordering integer.

### FenrirRenderer: the image 2D target and groups (ADR-0060)

- The 2D render feature gains an **image 2D target**: a `DrawList2D` drawn into an `Image` asset of a given size, premultiplied RGBA8, encoded sRGB or linear as the list's root asks, with `localToClip` from the image's size and a content scale of 1, the scissor realised as the hardware scissor as on the window. A game submits a list to an image through the renderer's facade the way it submits to the overlay; the image is then any image asset (a material's texture, a UI image's source).
- A group's image is a **transient image** from a per-frame pool keyed by size and format, the group's bounds in target pixels rounded outward, drawn as an image target of its own whose local origin is the group's origin; the end-group command draws that image through the painter's shader as the composite says. `blur(r)` is a separable two-pass Gaussian at the group's resolution, at half resolution past a radius threshold, with one more pooled image; `drop-shadow` is the group's alpha blurred and tinted, drawn offset first; a mask multiplies the following commands' colour by the group's alpha through a texture slot; `backdrop-filter: blur` copies the target under the element's bounds into a pooled image, blurs it and draws it as the background, the one case that reads the target it draws into, which the window overlay allows and an image target allows by drawing to a copy. An empty group draws nothing.
- Window overlay and image targets both realise groups; the world-space target does too, since its list is drawn as one pass.
- The **world-space 2D target**, last: an entity with a component naming a UI root, its size in world units and its UI units per world unit; the renderer draws the root's list on that quad in the `Transparent` stage, sorted with the other transparents by its origin, with `localToClip` from the quad's world matrix and the view, the hardware scissor off and clips realised in the painter's shader from the vertex's clip data, the blend space linear. The game owns the entity's transform like any other.
- `RendererStats` gains the 2D counts: lists, commands, groups and transient image bytes per frame.

### FenrirText (ADR-0062)

- A header-and-sources package with HarfBuzz, FreeType, SheenBidi and libunibreak as submodules under its own `libs`, built static, warnings silenced as SDL's are, behind `FENRIR_ENABLE_TEXT` (on by default; FenrirPainter and FenrirUI require it).
- **Font asset**: FenrirAssets gains a `Font` asset type, bytes of a TrueType or OpenType file loaded through the asset store like an image, with a revision. FenrirText opens faces from it (FreeType face and HarfBuzz font) and keeps them in a font cache keyed by asset and size.
- **Font family registry**: a family name maps to faces by weight and style, with a fallback list per family and the engine's embedded default face (the debug font's JetBrains Mono, OFL, already in the renderer's resources; FenrirText embeds its own copy so the renderer stays independent) as the final fallback. Stylesheets register families through `@font-face`; code registers them directly.
- **Shaping**: a paragraph's text (UTF-8) is split into runs by style, script and bidi level (SheenBidi), each run shaped by HarfBuzz into glyphs with advances and offsets, then broken into lines by libunibreak's UAX#14 opportunities and the width, reordered per line by bidi level, and aligned. Grapheme boundaries (UAX#29) are exposed for carets.
- **Paragraph API**: a paragraph takes runs of text with a text style (family, size, weight, style, colour, letter spacing, decoration), paragraph style (alignment, line height, max lines, ellipsis, white-space), lays out to a width, and answers its size, line metrics, glyph runs with positions and atlas rectangles, the rectangle of a character range, and the character at a point. A measured paragraph is reusable across frames and relaid out only when text, style or width changes.
- **Atlases**: one per face, size and mode, a skyline-packed R8 texture (an `Image` asset FenrirText owns) that doubles when full up to a limit and then opens a second page, uploaded by dirty rectangle through the asset store's image update. Bitmap mode rasterises with FreeType's auto-hinter and grayscale anti-aliasing at the exact pixel size (the root's scale times the style size, rounded to a quarter pixel for positioning); SDF mode rasterises FreeType's single-channel SDF at one size per face with a spread and the shader reconstructs any size. Which mode a font uses is a property of the font family's registration and of `@font-face`.
- The renderer's `DebugFont` is untouched.

### FenrirPainter (ADR-0060)

- A `Painter` is a value constructed over a `DrawList2D`, a root scale and a blend space; it appends and keeps nothing between frames. Coordinates are UI units; the painter multiplies by the root scale into the list's logical units and snaps rectangles to whole pixels when asked.
- **Paths**: move, line, quadratic, cubic, arc, arc-to, close; helpers for rectangle, rounded rectangle with per-corner radii, circle, ellipse; a fill rule of non-zero (default) or even-odd. A path is a value the caller may keep.
- **Paint**: solid colour; linear gradient (two points, stops, spread pad or repeat); radial gradient (centre, radius, focal point, stops); image pattern (image asset, sampler, rectangle, nine-slice insets); each with its own transform. Colours are sRGB with straight alpha at the API and premultiplied in the vertex.
- **Stroke style**: width, cap (butt, round, square), join (miter, round, bevel), miter limit, dash array and offset. Hairlines are a one-pixel stroke.
- **Draws**: fill and stroke of a path; rectangle, rounded rectangle, border (per-side widths and colours, per-corner radii), shadow (rectangle, radii, offset, blur, spread, inset or outset), circle, arc, ring; image into a rectangle; a laid-out paragraph or a glyph run at a point. Paint is passed per call (Skia's shape, one native with an `in` paint for the facade).
- **State**: a save/restore stack of transform (translate, scale, rotate, concat) and clip (rectangle, rounded rectangle under the current transform; a path clip opens a mask group); begin and end group with opacity, blend mode and filter.
- **Technique**: a rectangle, rounded rectangle, border, shadow, circle, arc, ring or gradient fill is one quad whose fat vertices carry the shape (rectangle, radii, border widths and colours, shadow parameters), the paint (kind, transform, extents, stops as two colours plus a stop texture row for more, image slot) and the clip (rectangle, radii, transform), evaluated analytically in the fragment shader with signed-distance anti-aliasing over one pixel; an arbitrary path, polygon or stroke is flattened to a tolerance in pixels, triangulated (ear clipping, split by winding and holes), stroked into joins and caps, given a one-pixel fringe, and emitted through the same vertex with its shape kind set to "triangle" so the shader only applies paint and clip. Text is quads over the atlas with kind "glyph" (bitmap) or "sdf glyph".
- **Batching**: consecutive draws with the same pipeline, texture slots and hardware scissor extend one command; a group boundary, a texture change or a scissor change starts another. The hardware scissor is the clip stack's intersected bounds; the per-pixel clip handles the radii and rotation.
- **Shader**: one painter shader asset (`builtin:painter`) with the engine's 2D set 0, a vertex stage reading the fat layout, and a fragment stage switching on the vertex's shape kind; variants for the gamma blend space and for the composite kinds (plain, blur pass, drop shadow, mask).
- **Budget**: a `DrawList2D`'s 16-bit indices are kept by starting a new command with a base vertex when a batch passes 65535 vertices; 32-bit indices are a painter option.

### FenrirUI (ADR-0063 to 0066)

- **Storage**: elements live in a dense tree per UI root, addressed by `ElementId` (index and version); children are ordered; each element has a tag name, an id string, classes, pseudo-class flags, inline style, resolved style, Yoga node, laid-out rectangles (border box, padding box, content box, clip rectangle) in UI units and in target logical units, a text paragraph when it has text, and a control behaviour when it is a control.
- **UI root**: owns its elements, its stylesheets, its target (window overlay layer, image, or world quad), its scale rule, its blend space, its input router registration and priority, its focus, its hot-reload switch and the file list it watches. A game makes roots through the UI's app resource and ticks happen for all of them.
- **Frame**: in `PreUpdate` FenrirInput dispatches to the roots' routers; in `PostUpdate` each root advances transitions and animations, resolves dirty styles (the cascade walks matching rules bucketed by type, class and id, then inherits, then animates), lays out if the Yoga root is dirty, runs post-layout (rectangles snapped to pixels, `:intro` cleared after the first completed layout, scroll offsets clamped), runs the hosted paint stage for C#, paints every element in tree order (background and border as one box draw, then content, then children sorted by `z-index`, inside the element's overflow clip and opacity group when needed, shadows before the box), and submits the list to its target. Deferred removals run after painting.
- **Layout bridge**: resolved style to Yoga style (flex, size, inset, margin, padding, border, gap, position, display, aspect ratio), a measure function for leaves (text from FenrirText, images from their size), rectangles read back. Yoga appears in no public header. Grid, when wanted, is a container element that measures itself and places its children; not in this spec.
- **Scale rule** per root: `1080-height` (default; scale = target pixel height / 1080 / content scale, so 1080 UI units span the target's pixel height) or `content-scale` (scale = 1, UI units are logical units). Pixel snapping is a per-root switch, on by default.
- **Stylesheet**: a parser of its own for the CSS subset of ADR-0064: rules with selector lists (type, `.class`, `#id`, descendant, `>` child, pseudo-classes `:hover :active :focus :focus-within :disabled :checked :first-child :last-child :intro :outro`, `:root`), declarations with the property set below, custom properties and `var()` with a fallback, nesting with `&`, `@import`, `@keyframes`, `@font-face`, comments; shorthand for margin, padding, border, border-radius, background, flex, transition and animation; lengths in px (UI units) and %, colours as hex, `rgb()`, `rgba()`, `hsl()` and the named set, `url()`, `linear-gradient()` and `radial-gradient()`. Unknown properties and malformed declarations are logged with file and line and skipped; a malformed rule skips the rule, never the sheet. Specificity and order resolve conflicts as CSS does; inline style wins; `!important` is not supported.
- **Property set**: display (flex, none), position (relative, absolute), inset (top, right, bottom, left), width, height, min-width, min-height, max-width, max-height, aspect-ratio, margin, padding, border-width per side, flex-direction, flex-wrap, flex-grow, flex-shrink, flex-basis, align-items, align-self, align-content, justify-content, gap, row-gap, column-gap; background-color, background-image (`url()` or a gradient), background-size (cover, contain, stretch), background-position, border-image for nine-slice, border-color per side, border-radius per corner; box-shadow (outset, several); opacity; transform (translate, scale, rotate, with transform-origin); overflow (visible, hidden, scroll) and scrollbar colour and width; cursor; pointer-events (none, all); z-index; color, font-family, font-size, font-weight, font-style, text-align, line-height, letter-spacing, text-shadow, white-space (normal, nowrap), text-overflow (clip, ellipsis); filter (blur, drop-shadow), backdrop-filter (blur); transition-property, transition-duration, transition-timing-function (the CSS named set, `cubic-bezier()`, `steps()`), transition-delay; animation-name, -duration, -timing-function, -delay, -iteration-count, -direction, -fill-mode. Inherited properties are the text ones, color, cursor and pointer-events.
- **Markup document**: a parser of its own for the XML-like grammar of ADR-0064: tags for every registered element (built-in and custom), `id`, `class`, `style`, control attributes as strings the control parses, bare text content assigned to the element's text when it has one and a load error otherwise, `<include src>` splicing with recursion detection, `<style src>` and inline `<style>`, the five XML entities and numeric ones, comments. Loading returns the root element created and a list of errors with file and line; a document with errors loads what it could.
- **Hot reload**: a root remembers every file it loaded (documents, includes, stylesheets, imports) with its modification time and, with the switch on, polls them every 250 ms; a changed stylesheet re-parses and dirties every style; a changed document reloads into the same parent position and the game is told through an engine event so it can re-find ids.
- **Input and focus**: a root's router hit-tests the pointer against elements with `pointer-events: all`, topmost first within z-index then tree order, honouring rounded corners and overflow clips; it tracks hover and pressed, sends the pointer events, and consumes an input when it hit; keyboard and text inputs go to the focused element and are consumed when it takes them; the router publishes pointer, keyboard-focus and text-input claims (with the caret rectangle of a focused text entry). Focus moves by click (the nearest focusable ancestor of the hit), Tab and Shift+Tab in tree order, code, and gamepad direction (the focusable element whose rectangle is nearest in the stick's direction, Unity's and Godot's rule); a focused element gets `:focus`, its ancestors `:focus-within`; the virtual cursor is a per-root switch that turns stick motion into pointer motion and a button into a click. Escape, Enter and Space are default actions of controls.
- **Events**: a `UIEvent` shared engine event (root, target element, kind, pointer position in UI units, button, key, a float value and an integer value) sent once per event at the target; C++ listeners per element per kind, run target-first then bubbling with stop-propagation. Kinds: pointer enter, leave, move, press, release, click, double click, scroll; key press, release; text input; focus, blur; change; submit; document reloaded.
- **Controls**: panel; label (text, paragraph from style); image (source asset, fit); button (press and release within bounds is a click; Enter and Space when focused); checkbox (checked, toggles on click, `:checked`); slider (min, max, step, value; drag, arrow keys, wheel); progress (min, max, value); scroll view (vertical and horizontal, wheel, scrollbar drag, content drag with a threshold, gamepad scroll when focused); text entry (ADR-0066's edit model: single line, caret and anchor on grapheme boundaries, arrows, Home, End, Shift, Ctrl+arrows by word, Backspace, Delete, Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V through FenrirWindow's clipboard, Enter submits, Escape blurs, click places, drag selects, double-click selects a word, password masking, max length, placeholder; publishes the text-input claim with its caret). Each is an element with a behaviour and a rule block in the default stylesheet.
- **Default stylesheet**: embedded, plain, neutral greys, one sans family resolved to the embedded default face, visible focus rings, enough to make every control usable; a game replaces or extends it.
- **Lifecycle**: `:intro` on creation until the first completed layout; `Remove()` adds `:outro` and defers removal until running transitions finish, or removes at once with a flag; a removed element's id is stale thereafter.
- **Custom elements in C++**: subclass with measure and paint virtuals and optional input handling, registered under a tag name with a factory; the markup parser and code creation both go through the registry.
- **Switches**: per root, scale rule, blend space, pixel snapping, hot reload, virtual cursor; per UI app resource, anti-aliasing (fringe and analytic AA on, or off for pixel-art UIs), offscreen groups (off draws the group's content flat and ignores filters), a transient image budget.
- **Multiple windows**: a root names its window; roots on different windows are independent.

### Scripting (ADR-0061, ADR-0042)

- FenrirText, FenrirPainter and FenrirUI each get an API description, a generated binding and a contract assembly, deployed beside `Fenrir.Scripting.dll` like FenrirPhysics's; FenrirUI's contract builds on the painter's, the painter's on FenrirMath's.
- **Painter facade**: a static class with one native per draw call and state call, taking paths as handles (a path built through natives) or as spans of points, paints and stroke styles as `in` shared structs, colours and rectangles as mirrored math types; a painter is a wrapper handle a C# system obtains from the element it paints in the hosted stage, or from a draw list it owns to submit to the overlay.
- **Elements**: `ElementId` as a wrapper struct; natives for create, remove, parent and reorder, tag, id, classes (add, remove, toggle, has), inline style by property name and string value, text, control values, laid-out rectangles, queries (by id, by class, children), focus, and loading documents and stylesheets by path into a root; UI roots as wrapper handles made through natives on the UI's facade.
- **Events**: `UIEvent` mirrored as a shared event, read through `EventReader<UIEvent>`.
- **Hosted stage**: `Fenrir.UI.Paint`, after layout and before painting, in which a C# system paints into elements it owns through the facade; the UI keeps a per-element painter the C++ paint pass then appends.
- **Custom elements in C#** are composition of built-ins plus painting in the stage; subclassing is C++ only.

### UIDemo

A new example app with its own scene (a lit room from existing demo assets, no large downloads, ADR-0052):

- **HUD root** on the window overlay: health and stamina bars, an ammo counter, a compass driven by the camera's yaw through a C++ custom element painting with the painter, a minimap as an image element showing a top-down camera's render target, hit markers that `:intro` in and `:outro` out, all with `pointer-events: none`.
- **Menu root** on the window overlay, opened with Escape: a frosted pause panel (`backdrop-filter: blur`) with buttons, a settings panel with sliders, checkboxes, a text entry for a player name, a scrolling list, transitions on hover and focus, `:intro`/`:outro` on the panel, gamepad navigation with a visible focus ring and the virtual cursor on a toggle, hot reload of its stylesheet with the file path shown.
- **Texture root**: the same menu document drawn into an image target and shown on a material on a screen mesh in the room, to show a UI in a texture.
- **World-space root**: an interactive terminal panel on a quad in the room, with buttons and a slider that change the room's light colour, hit by a pointer ray from the camera through the cursor, its text in SDF mode, its clips pixel-exact under rotation; a name plate root above a capsule that faces the camera.
- **C# variant**: with scripting on, a C# system draws a second HUD element (a speedometer) through the painter facade in the hosted paint stage and reads menu clicks through `EventReader<UIEvent>`.
- The demo lists its switches on screen as LightingDemo does, and its README names what each screen shows.

## Testing Decisions

A good test drives a public seam and asserts on what comes out, never on how: element rectangles, engine events, draw-list contents, paragraph output and recorded renderer commands. The seams, highest first:

1. **A headless App with the null backend and FenrirInput's test feed** (prior art: `FenrirInputTests` drive the App a frame at a time with no window; the scripting tests' harness). A test builds a root from code or from markup and a stylesheet (inline strings, or files written to a temporary directory for hot reload), ticks the App, feeds pointer, key, text and gamepad input, and reads back element rectangles, `UIEvent`s through an `EventReader`, control values, pseudo-classes and the `DrawList2D` the root submitted (through a test target that captures it). This seam tests the cascade (specificity, inheritance, custom properties, nesting, transitions over ticks), the layout bridge against **Yoga's generated conformance fixtures** converted to trees in and rectangles out, the markup parser (good documents, every error kind with its line), controls, focus and gamepad navigation, `:intro`/`:outro` and deferred removal, hot reload, the scale rules, and the C# surface through the scripting harness (a script builds the same UI and reads the same events).
2. **`DrawList2D` contents** (prior art: `FenrirDraw2DTests`). The painter is tested by what it appends: one quad per analytic shape with the expected shape data, the triangle count and fringe of a tessellated path within a tolerance, scissor bounds from the clip stack, group begin and end commands balanced and nested, premultiplied colours, text quads over the atlas, batching (one command for same-state draws, a new one on texture, scissor or group change), 16-bit index rollover.
3. **FenrirText's paragraph output**: text, styles and a width in; glyph runs, line breaks, bidi order, alignment, ellipsis, hit positions and grapheme boundaries out, with HarfBuzz's test fonts and the embedded default face; the atlas's packing and growth by its reported rectangles and uploads through the asset store's image updates.
4. **The renderer's null backend recording** (prior art: ADR-0057's indirect draws and the 2D feature's existing tests): a list with nested groups and filters produces the expected transient images, passes and composite draws; an image target produces an image asset of the right size and encoding; the world-space target produces one transparent draw with the hardware scissor off.

No golden images in this spec: they need a display and a tolerance model and are deferred; UIDemo is the visual check. Each package's tests build with `FENRIR_BUILD_TESTS` and reach no GPU; the shader compiles through the existing shader build and is covered by the null backend's pipeline creation. CI runs what the build-and-test workflow already runs; read it before claiming coverage.

## Out of Scope

- A Razor-style C# syntax and any binding language (`{{ }}`) in markup or code: a later effort over this API (ADR-0061, ADR-0065).
- Block flow, CSS grid and inline text flow in layout; a grid container is the named first extension (ADR-0064).
- Multi-channel SDF (msdfgen), LCD subpixel text, colour emoji, variable fonts, system font fallback, IME composition display (FenrirInput's reserved shape), undo in text entry.
- Dropdown, tabs, virtualised lists, tree view, multi-line text area, tooltips as a built-in, drag and drop.
- Inset box shadows, `mask-image`, `mix-blend-mode`, `calc()`, `!important`, media queries, `border-image` beyond nine-slice, 3D transforms.
- A general file watcher in FenrirAssets (its own ADR); reload of fonts and images.
- Per-element draw caching with invalidation (ADR-0063 leaves the door open).
- A menu stack, screen manager, navigation history, or a theme beyond the plain default stylesheet.
- Lighting of world-space UI; occlusion of world UI by scene geometry beyond ordinary transparent sorting.
- Accessibility (screen readers, high-contrast modes), touch and pen input, a visual editor.
- Golden-image tests.
- Moving the renderer's debug text onto FenrirText.

## Further Notes

- **Ticket shape**: four packages, a renderer change and a demo, which is large enough that `/to-tickets` should be its own decision; the natural stack is FenrirDraw2D groups and the renderer's image target, FenrirText, FenrirPainter, FenrirUI core (tree, style, layout, paint), input and controls, markup and hot reload, scripting bindings, UIDemo, world-space target last.
- **Research**: the three notes under `research/` are the facts this spec stands on and stay with it until the feature is built.
- **Dependencies to add as submodules**: Yoga, HarfBuzz, FreeType, SheenBidi, libunibreak; each pinned, built static, warnings silenced, marked `SYSTEM`, following `libs/SDL`'s pattern; HarfBuzz is built with its FreeType integration on, so it shapes from FreeType's faces directly.
- **Vocabulary**: see GLOSSARY.md's "FenrirEngine painting and UI" section (painter, paint, element, element id, UI root, control, markup document, stylesheet, scale rule) and "offscreen group" under 2D drawing.
