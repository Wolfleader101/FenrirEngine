# 07: FenrirUI core: roots, elements, Yoga layout, inline style and box painting

**What to build:** a tree of styled boxes built from C++ lays itself out with flexbox and appears on the window every frame (spec: "FenrirUI" storage, root, frame, layout bridge, scale rule, switches; ADR-0063, ADR-0064, ADR-0066).

- **The package** depends on FenrirPainter, FenrirInput and FenrirApp; **Yoga** as a submodule under its `libs`, pinned, static, `SYSTEM`, used only inside the layout bridge and in no public header. Behind `FENRIR_ENABLE_UI`, on by default.
- **UI roots** through a UI app resource: a root names its window and overlay layer, its scale rule (`1080-height` default, or `content-scale`), its blend space (gamma on the window by default), pixel snapping; several roots coexist, ordered by layer.
- **Elements**: a dense tree per root addressed by `ElementId` (index and version, stale after removal); tag name, id string, classes, pseudo-class flags, inline style, resolved style, children in order; create, remove (immediate this ticket; deferred is ticket 10), reparent, reorder, find by id, by class, children; the `Element` base class with measure, paint and input virtuals and a registry of tags with factories, so custom elements subclass it (controls are such subclasses from ticket 12).
- **Inline style**: the property set of the spec settable by typed setters and by property name and string value, this ticket for layout and visual properties (display, position, insets, sizes, margin, padding, border widths, flex properties, gap, aspect ratio, background colour, gradient and image with fit and nine-slice, border colours and radii, box shadows, opacity, transform with origin, overflow visible and hidden, z-index, pointer-events, cursor); text properties come with ticket 09, transitions with 10. No stylesheet yet (ticket 08): the resolved style is the inline style over defaults and inheritance.
- **Layout bridge**: resolved style to Yoga style, dirty-path marking, a measure function hook for leaves (images measure their asset's size; text in 09), rectangles read back in UI units and snapped to target pixels.
- **Frame**: `PostUpdate` for each root: resolve dirty styles, lay out if dirty, post-layout, paint every element in tree order (shadows, then background and border as one box draw, children by z-index inside the overflow clip and an opacity group when opacity is under one and the element has children, transforms applied), submit to the overlay. Switches: anti-aliasing and groups on the UI app resource.
- A README in the package's style; the renderer's README points to it from the overlay section.

**Blocked by:** 03, 05

**Touches:** `packages/FenrirUI/`, `packages/CMakeLists.txt`, `CMakeLists.txt`, `.gitmodules`, `README.md`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] Headless App tests (null backend): Yoga's generated conformance fixtures, converted to element trees with inline styles, lay out to the fixtures' rectangles through the bridge; absolute positioning, percentages, gaps, min and max; both scale rules at two window sizes; pixel snapping rounds consistently.
- [x] Tree tests: ids go stale on removal; reorder and reparent keep children ordered; queries by id and class; a dirty subtree relays out and a clean root does not call Yoga.
- [x] Draw-list tests through a capturing target: a tree of three nested panels with radii, a border, a shadow and half opacity on a parent emits the expected box draws, clip data and one group, in tree order.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
