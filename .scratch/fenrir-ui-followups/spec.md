Status: ready-for-agent

# FenrirUI follow-ups: element filters, blurred text shadows, the window cursor, the rest of the property set, `currentcolor`, image and world roots from C#, and test and demo cleanups

Origin: the FenrirUI close-out review (PR #362, "Left for the user"), whose spec gaps are new features rather than review fixes, plus its cleanups. FenrirUI (ADR-0060 to ADR-0066) and the Razor UI on top of it (ADR-0067 to ADR-0070) are built and on main (#378); their specs are deleted, and `git show 70155058:.scratch/fenrir-ui/spec.md` holds the FenrirUI one this cites ("Property set", story 31, story 64). Each item below was checked against main at `10f8b07d`. Settled with the user on 2026-11-16: the transient image budget is dropped; C# gets image and world roots, amending ADR-0061; a window root applies `cursor` to the window by default.

## Problem Statement

A designer's stylesheet for FenrirUI mostly works, but some of what the spec promised is parsed and then quietly does nothing:

- `filter: blur()` and `drop-shadow()` style nothing (story 31), though FenrirPainter's offscreen groups and the renderer already take both filters. Box painting opens a group for opacity only.
- A `text-shadow`'s blur radius is parsed and not drawn: the shadow is a sharp copy of the text.
- `background-position`, `scrollbar-color` and `scrollbar-width` are parsed and dropped (`StyleValues.cpp`'s `LaterLonghands`), with no log. The `background` shorthand writes a position nothing reads.
- `cursor` is resolved and inherited, and nothing sets the window's cursor from it, though `Window::SetCursorShape` exists. A button shows the arrow.
- The `inset` and `border-image` shorthands are unknown properties; only their longhands exist.
- `currentcolor` is not a colour, so the `border` shorthand's colour left out is black where CSS gives the text colour.

C# cannot make an image root or a world root, or give a world root its pointer ray: `UIRoot.Create` takes `RootSettings`, a window and a layer. A UI built in C# and one built in C++ are not interchangeable (story 64).

And the close-out left cleanups: `CaptureTarget` is written three times (FenrirUI's tests, the UI scripting tests, UIDemo's tests); about 60 FenrirUI tests REQUIRE their setup before their first INFO, so a failure there reports no behaviour; `Text.cpp`'s label-size checks take the expected value from the label's own paragraph; `YawPitch` is written three times and a screen quad twice across UIDemo's `Room.cpp` and RendererDemo.

## Solution

FenrirUI paints the missing properties through what the painter already has, applies `cursor` to the window from window roots, accepts the two shorthands and `currentcolor`, and its scripting extension makes image and world roots. Test support and demo helpers each move to one place. Nothing new in FenrirPainter, FenrirDraw2D or the renderer, except one `CursorShape` in FenrirWindow.

## User Stories

1. As a UI developer, I want `filter: blur(4px)` to blur an element and its subtree as one, so that a panel behind a dialog reads as out of focus.
2. As a UI developer, I want `filter: drop-shadow(0 4px 8px black)` to cast a shadow of what the element and its subtree draw, so that an icon or a rounded panel with a cut-out casts a shadow of its own shape, not its box.
3. As a UI developer, I want a list of filter functions applied in order, so that `drop-shadow(...) blur(...)` is what CSS gives.
4. As a UI developer, I want a filter to fade with the element's opacity and animate through transitions, so that a blur eases in.
5. As a UI developer, I want a `text-shadow`'s blur drawn, so that a glow or a soft shadow under text looks as it does on the web.
6. As a game developer, I want `UISettings::offscreenGroups = false` to ignore filters and draw text shadows sharp, so that the one switch keeps cheap hardware cheap.
7. As a UI developer, I want `background-position` to place a background image that does not fill its box, so that `cover` crops at the centre or a corner I choose and a small image sits where I say.
8. As a UI developer, I want `scrollbar-width` and `scrollbar-color` on a scroll view, so that the standard properties style its bars without `::scrollbar` rules.
9. As a player, I want the pointing hand over a button and the I-beam over a text entry, so that I can tell what will take a click.
10. As a UI developer, I want `cursor` on any element to set the window's cursor while the pointer is over it, so that a drag handle shows a resize cursor.
11. As a game developer, I want a window root to leave the cursor alone while the cursor is captured or hidden for mouse look, and to give the arrow back when the pointer leaves the UI, so that the UI never fights my camera.
12. As a game developer, I want to turn a window root's cursor off, so that a game that draws its own cursor keeps it.
13. As a game developer, I want to read the cursor a world root's hovered element wants, so that I can apply it while my ray is on the quad.
14. As a UI developer, I want `inset: 0` and `border-image: url(frame.png) 12`, so that the shorthands I write on the web work here.
15. As a UI developer, I want `currentcolor`, and a border or shadow colour left out to be the text colour, so that an icon's border follows its label's colour through hover and transitions.
16. As a C# game developer, I want to make an image root and read its image, so that a C# script can put a UI on a material.
17. As a C# game developer, I want to make a world root on an entity's quad and give it a pointer ray each frame, so that an in-world terminal can be written in C# alone.
18. As an engine developer, I want one `CaptureTarget` for every UI test, so that a change to `UITarget` is made once.
19. As an engine developer, I want every UI test's first REQUIRE under an INFO, so that a failing setup says what went wrong.
20. As an engine developer, I want the demos' `YawPitch` and screen quad in DemoScaffold, so that a fix to one reaches every demo.

## Implementation Decisions

### Filters (ticket 03)

- **`filter`** becomes a styled property: `none`, or a list of `blur(<length>)` and `drop-shadow(<offset-x> <offset-y> [<blur>] [<colour>])`, a length in UI units and 0 or more, the colour `currentcolor` when left out (ticket 06 makes it a value; until then black). Not inherited. Anything else is the usual logged parse error with the declaration skipped.
- **Painting.** An element with a filter draws itself and its subtree through one offscreen group per function, through FenrirPainter's `GroupFilter::Blur` and `GroupFilter::DropShadow`; the first function is the innermost group, as CSS applies them in order. The groups' bounds are what the element and its subtree cover (`LocalBounds`), grown by each filter's reach (a blur's radius; a shadow's offset plus its blur). CSS's order holds: filter, then clip, then opacity, so an opacity group, when there is one, is outside the filter groups. The nesting limit (8, ADR-0060) counts these groups; past it a group draws flat, as the painter already does, logged.
- **Animation.** `filter` transitions and animates when both ends are lists of the same functions in the same order, each length and colour mixed; otherwise it switches at the midpoint, as CSS's discrete fallback.
- **A text shadow's blur.** A shadow with a blur draws its runs in its colour inside an offscreen group with `GroupFilter::Blur(blur)`, bounded by the paragraph's box at the shadow's offset grown by the blur; a shadow with no blur stays a direct draw. Several blurred shadows are a group each.
- **`offscreenGroups` off** ignores filters (the element draws as with `filter: none`) and draws text shadows sharp, as the README's "Settings" says for groups. No new switch.

### The window cursor (ticket 04)

- `UIRootConfig` gains `applyCursor` (default true), meaningful for window roots only; image and world roots never touch the window.
- **Which shape.** The cursor of the root's hovered element, as resolved: `auto` is the I-beam over a text entry and the arrow elsewhere; `default` the arrow; `pointer` the hand; `text`, `not-allowed` and the four resizes their shapes; `move` a new `CursorShape::Move` in FenrirWindow (SDL's move cursor; the window binding regenerates); `none` `CursorShape::Hidden`.
- **When.** Each frame after input routing, the window root with `applyCursor` on whose router took the pointer (the highest-priority one hit-testing an element that takes pointer input) sets its shape through `Window::SetCursorShape`, only when it differs from the last shape FenrirUI set on that window. When no such root has the pointer and FenrirUI's last shape on the window was not the arrow, it sets the arrow once and then leaves the window to the game. Nothing is set while the window's cursor mode is not `Normal`. Elements with `pointer-events: none` are not hovered, so the HUD never sets a cursor.
- **Read for any root.** `UIRoot::GetHoveredCursor()` gives the hovered element's resolved shape as a `Cursor` (`auto` resolved as above), or `Cursor::Auto` with nothing hovered; a game applies a world root's itself. It is on the `UIRoot` mirror too.
- **The default stylesheet** gives `button`, `checkbox`, `slider` and the scroll view's thumb `cursor: pointer`, `text-entry` `cursor: text`, and `:disabled` controls `cursor: default`.

### The remaining longhands (ticket 05)

- **`background-position`**: one or two of `left`, `center`, `right`, `top`, `bottom`, lengths and percentages, CSS's meaning (a percentage aligns that point of the image with that point of the box); initial `0% 0%`. It places a background image drawn at `cover` (which part is cropped) or `contain` (where the image sits) in the border box; `stretch` fills the box and ignores it. Gradients ignore it. It transitions as two lengths.
- **`scrollbar-width`** (`auto`, `thin`, `none`) and **`scrollbar-color`** (`auto`, or a thumb colour then a track colour) on a scroll view, not inherited. Where either is not `auto` it wins over the `::scrollbar` and `::scrollbar-thumb` width and background colours, as Chrome does since 121: `thin` is half the default stylesheet's bar width, `none` hides the bars while the view still scrolls. `auto` leaves the pseudo-elements' styles in charge, so every stylesheet written so far is unchanged.
- `LaterLonghands` is then empty and removed: no property is parsed and ignored.

### Shorthands and `currentcolor` (ticket 06)

- **`inset`**: one to four values, top first, into `top`, `right`, `bottom` and `left`, as `margin` expands.
- **`border-image`**: `<source> [<slice>]`, into `border-image-source` and `border-image-slice` (one to four slices in the image's pixels, top first; left out, the longhand's initial 0); a `/ width`, `/ outset` or a repeat keyword is a logged parse error naming the longhands FenrirUI has, since it has none of those.
- **`currentcolor`** (any case) is a colour wherever a colour is taken: `background-color`, the border colours, shadow and text-shadow colours, `drop-shadow()`'s, `scrollbar-color`'s, gradient stops, and the pseudo-elements' colours. It is kept as such through the cascade and resolved to the element's own `color` after it, every frame the colour can change, so it follows `color`'s transitions. `color: currentcolor` is `color: inherit`.
- **CSS's initial values**: the border colours and a shadow's colour left out are `currentcolor` (the `border` shorthand's colour too). This changes what an existing stylesheet draws where it relied on black; the default stylesheet and UIDemo's sheets are checked and given explicit colours where they want black.

### Image and world roots from C# (ticket 07; amends ADR-0061)

- Two shared structs beside `RootSettings` in `ScriptUI.hpp`: **`ImageRootSettings`** (width and height in pixels, colour space, scale rule, snapping, the default stylesheet, hot reload) and **`WorldRootSettings`** (the entity, scale rule, snapping, the default stylesheet, hot reload, virtual cursor). Their defaults are the C++ targets' and `UIRootConfig`'s.
- Natives, facades on the `UIRoot` mirror: `UIRoot.CreateImageRoot(settings)` and `UIRoot.CreateWorldRoot(settings)` returning the root (an invalid one, warned once, without the UI resource, an asset store for an image, or a `WorldQuad2D` on the entity for a world root); `GetImage()` giving the image root's `AssetHandle<Image>`, empty for other roots; `SetPointerRay(ray, pressed)` as `UIRoot::SetPointerRay`, a no-op with a warning on a root that takes no ray. `Destroy` already serves every kind. The extension owns each target it makes and ends it with the root.
- `UIRoot.Find` keeps finding window roots only; a script keeps the `UIRoot` it made.
- **ADR-0061** gains a dated amendment: C# makes every kind of root C++ makes, through natives on the `UIRoot` mirror, the scripting extension owning the targets; and the FenrirUI scripting README says so.

### Dropped: the transient image budget

The FenrirUI spec's "Switches" listed a per-app transient image budget. It is dropped (the user, 2026-11-16): `UISettings::offscreenGroups` and `RendererSettings::offscreenGroups2D` are the switches for cheap hardware, and `RendererStats::transientImageBytes2D` shows what groups cost. Nothing to amend: no ADR or README names the budget.

### Cleanups (tickets 01 and 02)

- **One `CaptureTarget`**: a `UITarget` recording what it is given, in a test-support header of FenrirUI's (an INTERFACE library, `FenrirUITestSupport`, built with the tests), used by FenrirUI's tests, the UI scripting tests and UIDemo's tests. The three copies are deleted; where they differ, the shared one takes the union, each option documented.
- **INFO first**: every FenrirUI and UI scripting test case's first REQUIRE is under an INFO saying what the setup was meant to give, per `CODING_STANDARDS.md`.
- **`Text.cpp`'s label sizes**: the expected size comes from a paragraph laid out by the test itself through FenrirText (same face, size and scale), or from a literal, never from the label's own paragraph.
- **DemoScaffold** gains `YawPitch(yaw, pitch)` and a screen-quad mesh builder (`MeshData` of a width, a height and a texture repeat, facing +Z), each documented; UIDemo's `Room.cpp` and RendererDemo's `CamerasAndTargets.cpp` and `MeshesAndMaterials.cpp` use them. A refactor: every demo draws and every test passes as before.

### Order

Tickets 01, 03, 04, 05, 06 and 07 share FenrirUI's tests and README, so they are one stack in that order, each branch cut from the one before. Ticket 02 touches only the examples' sources and DemoScaffold, and runs alongside, off the base.

## Testing Decisions

- Tests cross FenrirUI's public seam: a root on the shared `CaptureTarget`, its recorded `DrawList2D` (begin and end group commands with their filters, draws, image positions), `UIRoot`'s getters, and the stylesheet parser's results and logs. The expected values are literals or worked CSS examples, never the code under test (prior art: `Painting.cpp`, `StylesheetParser.cpp`, `Input.cpp`, `ImageRoots.cpp`, `WorldRoots.cpp`).
- The window cursor is tested through FenrirWindow's headless window and `Window`'s last-set shape, with FenrirInput's test pointer moving over a button, a text entry, a `pointer-events: none` element and nothing, and with the cursor mode `Captured`.
- C# roots are tested in the UI scripting tests (`Roots.cpp`'s harness): make, draw into, read the image of and destroy an image root; make a world root on a quad entity and click a button by ray; the warnings once each.
- The renderer is not retested: its filters and groups are ADR-0060's and tested there.
- UIDemo shows each: a drop shadow on the HUD's compass and ammo counter, and the HUD blurred (`filter: blur()`, transitioned) while the pause menu is open (ticket 03), a glow on the hit marker's text (03), the hand and I-beam over the menu and the terminal (04), a positioned background and a thin scrollbar in the controls list (05), `inset: 0` and a `currentcolor` border on the terminal (06), and a C# world root, a small sign whose button logs, in the C# variant (07). Each ticket adds its line to UIDemo's hand checks.

## Out of Scope

- The transient image budget (dropped above).
- `backdrop-filter` with a mask, and filter functions beyond `blur` and `drop-shadow` (brightness, contrast, grayscale and the rest).
- `border-image-width`, `-outset` and `-repeat`, and several background layers.
- UIDemo's volume slider applying a volume: it waits on an audio package.
- ADR-0027's 2D material limits (4 texture slots, 128 parameter bytes) are still the first version's; nothing here needs more.

## Tickets

Built on the integration branch `ui-followups`, one session, branch (`uif/NN-<slug>`) and PR per ticket, stacked as "Order" says. The orchestrator fills the PR column; ticket sessions leave this table alone.

| Ticket | Branch | Stacked on | PR |
| --- | --- | --- | --- |
| 01 One CaptureTarget and test cleanups | `uif/01-test-support` | `ui-followups` | #383 |
| 02 Demo helpers in DemoScaffold | `uif/02-demo-helpers` | `ui-followups` | #382 |
| 03 Element filters and blurred text shadows | `uif/03-filters` | 01 | #385 |
| 04 The window cursor from `cursor` | `uif/04-cursor` | 03 | |
| 05 `background-position` and the scrollbar properties | `uif/05-longhands` | 04 | |
| 06 `inset`, `border-image` and `currentcolor` | `uif/06-currentcolor` | 05 | |
| 07 Image and world roots from C# | `uif/07-csharp-roots` | 06 | |
