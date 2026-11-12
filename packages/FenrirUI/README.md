# FenrirUI

UI roots of elements (ADR-0063, ADR-0066), laid out by Yoga flexbox behind a layout bridge (ADR-0064) and painted every frame through FenrirPainter into each root's 2D target. The tree is retained: style resolution and layout run for what is dirty, and the draw list is made again every frame. Elements are FenrirUI's own, addressed by versioned `ElementId`s, not entities. It depends on FenrirPainter, FenrirInput, FenrirApp and FenrirAssets; a root on a window reads the window's size from FenrirWindow and submits to FenrirRenderer's overlay, both linked privately, and Yoga (the `libs/yoga` submodule, 3.2.2, static) is included by `src/LayoutBridge.cpp` alone, so no Yoga type is in a public header. It is built with `FENRIR_ENABLE_UI` (on by default), which needs `FENRIR_ENABLE_RENDERER`. This is the core of the FenrirUI spec (`.scratch/fenrir-ui/spec.md`): stylesheets, text, transitions, input and focus, controls and markup come in later tickets.

## Installing

`UI::Install(app)`, after FenrirAssets' `Assets::Install`, installs the painter (`Painting::Install`), adds the `UIRoots` app resource and a sequential `PostUpdate` system that runs every root's frame. Destroying the owner removes the resource and every root. Without an asset store, or with a UI already installed, it logs why and its `IsValid()` is false.

## Roots

`UIRoots::CreateRoot(UIRootConfig)` makes a UI root; several coexist, drawn lowest `layer` first, then in the order they were made, and `DestroyRoot` ends one. A root names:

- **its target**: by default its `window`'s overlay on `layer` (ADR-0027), at the window's logical size and content scale; nothing is drawn until the renderer (which adds the overlay) and the window exist. A `UITarget` of the game's own (`FenrirUI/UITarget.hpp`) gives the size and takes each frame's `DrawList2D` instead, which is how the tests capture what a root draws.
- **its scale rule**: `ScaleRule::Height1080` (the default: 1080 UI units span the target's pixel height, so the scale is pixel height / 1080 / content scale logical units per UI unit, and a UI authored at 1080p keeps its proportions) or `ScaleRule::ContentScale` (one UI unit is one logical unit). `GetScale()` is the last frame's.
- **its blend space**: `Gamma` by default, as a UI on the window overlay blends (ADR-0065).
- **pixel snapping** (on by default): every edge of every laid-out rectangle is rounded to the target's nearest pixel, a half pixel up, so neighbours that share an edge share its pixel; the painter snaps what it draws too.

## Elements

A root starts with its root element (tag `root`), which fills the target and cannot be removed. `Create(parent, tag, index)` makes an element under a parent at an index (`UIRoot::End` for last); `Remove` takes it and everything under it away at once, and their ids go stale: a call with a stale id does nothing and returns an empty value, and a slot used again gets a new version. `Reparent` and `Reorder` move an element and its subtree, keeping every list of children in order. Each element has a tag name, an id string (`FindById`), classes (`AddClass`, `RemoveClass`, `FindByClass`, in tree order), pseudo-class flags (`SetPseudoClass`), an inline style, a resolved style and its rectangles (`GetBoxes`: border, padding and content boxes and the clip its ancestors set, in UI units from the root's top-left, before transforms).

`Element` (`FenrirUI/Element.hpp`) is an element's behaviour, which custom elements subclass: `HasMeasure` and `Measure` size a leaf's content box from the space layout offers (`InvalidateMeasure` asks again), `Paint` draws its content through the painter after its box and before its children, and `OnInput` is where the root's input router will route to it (nothing calls it yet). `UIRoots::RegisterElement(tag, factory)` registers a subclass under a tag; creating an unregistered tag makes a plain `Element`, which paints its box only.

## Style

The inline style is a `StyleDeclarations` (`FenrirUI/Style.hpp`): some properties given, the rest not. Give them through the typed setters (`root.EditInlineStyle(id)->SetWidth(Fenrir::Length::Px(200)).SetFlexDirection(Fenrir::FlexDirection::Row)`), or by CSS longhand name and value (`root.SetStyleProperty(id, "margin-left", "10px")`), which logs and refuses an unknown property or a value it cannot read. The properties:

- **Layout**: `display` (flex, none), `position` (relative, absolute), `top`, `right`, `bottom`, `left`, `width`, `height`, `min-width`, `min-height`, `max-width`, `max-height`, `aspect-ratio`, the `margin-*`s, `padding-*`s and `border-*-width`s, `flex-direction`, `flex-wrap`, `flex-grow`, `flex-shrink`, `flex-basis`, `align-items`, `align-self`, `align-content`, `justify-content`, `row-gap`, `column-gap`. Lengths are `px` (UI units), `%` or `auto` (`none` for a maximum).
- **Visual**: `background-color`; `background-image` (`url(path)`, loaded from the asset store, `linear-gradient()` with an angle or `to` side, or `radial-gradient()`, an ellipse to the farthest corner) with `background-size` (stretch, cover, contain); `border-image-source` and `border-image-slice` for a nine-slice image over the border box; the `border-*-color`s and `border-*-radius`es; `box-shadow` (outset shadows, a list); `opacity`; `transform` (`translate()`, `translateX()`, `translateY()`, `scale()`, `scaleX()`, `scaleY()`, `rotate()`) with `transform-origin`; `overflow` (visible, hidden); `z-index`; `pointer-events` (none, all; none by default, ADR-0065); `cursor`.
- Colours are `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, `hsl()`, `hsla()`, `transparent` or a CSS colour name, sRGB with straight alpha, kept linear as `Colour`.

Shorthands (`margin`, `padding`, `border`, `flex`, `background`, `gap`) come with the stylesheet. With no stylesheet yet, an element's resolved style is the defaults, then the inherited properties (`pointer-events` and `cursor`) from its parent, then its inline style. The defaults are Yoga's, as in React Native and Unity's USS rather than the web: `flex-direction` column, `flex-shrink` 0, `align-content` flex-start, and every element a flex container.

## The frame

In `PostUpdate`, for each root in drawing order: its target's size and the scale; the dirty styles resolved (a change of classes, pseudo-classes or inline style marks the element and the path above it; a changed inherited value reaches the children), each written into its Yoga node; a layout if any node is dirty (a clean root calls no layout, which `GetStats()` shows); the rectangles read back and snapped when it laid out or the pixel size changed; then every element painted in tree order, and the list submitted to the target.

Each element paints, under its transform about its origin: its shadows (the first on top), its background colour and image as one rounded rectangle each and its border, as the painter's box draws over its border box, and its border image; then its content (`Element::Paint`); then its children sorted by `z-index` (equal ones in tree order), inside its padding box with its inner radii when it hides overflow. An element whose opacity is under one and that has children draws all of that through an offscreen group (ADR-0060) over what it and its subtree cover, composited at its opacity; a leaf's colours are faded instead. An element that is `display: none` or fully transparent paints nothing.

`UIRoots::settings` holds the switches for every root: `offscreenGroups` (off, an element's subtree is faded box by box instead of through a group).

## Tests

With `FENRIR_BUILD_TESTS`: `FenrirUITests`, headless (no window: each root draws into a capturing `UITarget`), lays out Yoga's generated conformance fixtures, converted to element trees with inline styles by `Tests/fixtures/convert-yoga-fixtures.py` (run it again when the submodule moves; fixtures using what the property set lacks, such as start and end edges, are left out, and so are five that test Yoga's own layout root, which has no containing block, where each fixture's root here is an absolute child of the root element), and checks absolute positioning, percentages, gaps, minimums and maximums, both scale rules at two target sizes, pixel snapping, the tree's ids, order and queries, what a frame re-does when a subtree is dirty and when nothing is, custom elements and their measures, inheritance, root order, the draw list of a nested tree (shadow, boxes, border, overflow clip and one opacity group, in tree order), opacity without groups, z-index, transforms and backgrounds.
