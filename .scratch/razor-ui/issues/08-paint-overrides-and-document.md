# 08: Paint overrides and `<Document>`

**What to build:** a Razor component paints into its own element by overriding `OnPaint`, and places a markup document with `<Document Src>` (spec: "Components, mounting and the dispatcher", "The builder" unowned children; ADR-0067, ADR-0068).

- **`OnPaint`.** `UIComponent.OnPaint(PainterId painter, ElementBoxes boxes)`. A second shipped dispatcher system, `[StageSystem(UIStages.Paint)]`, calls it for each mounted component whose type overrides it. Overriding types are found once per type and cleared at release. The painter comes from `PaintInto(Element)` and the boxes from `GetBoxes(Element)`.
- **`<Document Src="…" />`.** A built-in component in `Fenrir.UI.Razor`. It makes a host panel and calls `LoadDocument` into it, again when `Src` changes, after removing the old content. The host's children are unowned, so the builder never visits or removes them except with the host. Load errors are logged as `LoadDocument` logs them.

**Blocked by:** 04

**Touches:** `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/Tests/`, `packages/FenrirUI/README.md`

Stacked on 07.

**Status:** ready-for-agent

- [ ] Scripting tests:
  - an `OnPaint` override's draws appear in the root's captured list after the element's own box;
  - a component without the override is never called in the paint stage;
  - `<Document Src>` places a document's elements, which C++ finds by id;
  - a rebuild of the placing component leaves those elements untouched;
  - changing `Src` replaces them;
  - unmounting removes them with the host.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
