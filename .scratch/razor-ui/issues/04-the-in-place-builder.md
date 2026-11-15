# 04: The in-place builder

**What to build:** the generated code of a `.razor` file makes, updates and removes elements through an element seam, keyed by sequence number, with no native call for a value that did not change (spec: "The builder"; ADR-0067).

- `ElementBuilder : RenderTreeBuilder` in `Fenrir.UI.Razor`, over an internal element seam whose production side is `UIRoot` and whose test side is a recording fake.
- **Keys and regions.** Keys are `(sequence, occurrence)`, or `(sequence, key)` with `@key`. A region per component, per fragment and per element. Each element is resolved at its first non-attribute call. A tag change at the same key replaces the element.
- **Values.** Class token diffs, style declaration diffs (a dropped declaration is reset with `""`), id, value by tag (`SetValue`, or `SetEntryText` for a `textentry`), other attributes through `SetAttribute`. Text runs are concatenated per element: `SetText` in a `label` or `button`, and a generated `label` elsewhere. Lowercase tags go through `CreateElement`. Each native is called only on change.
- **Folded markup.** `MarkupFragment` parses `AddMarkupContent` strings once, covering elements, attributes, text, entities, void elements and `<style>` (handed to a sheet hook ticket 07 fills; until then the hook is a no-op), into the same operations.
- **Removal and order.** An unvisited entry is removed with `Remove(Outro)` and forgotten. Moves use `Reorder`, outside the longest increasing subsequence only. Indices count children the builder does not own (outgoing elements, and children marked unowned for ticket 08).
- **Child components.** Found or made at their key, with parameters set by name through setters compiled once per type and cached. `ChildContent` and fragments get their own regions. In this ticket a child rebuilds whenever its parent does; ticket 05 adds change detection.
- `@ref` on elements (an `ElementId` through `ElementReference`, cleared on removal) and on components.
- A logged warning, once per sequence, for an unrecognised directive reaching `SetAttribute` (a name starting `@`, or `on` followed by a known event).

**Blocked by:** 03

**Touches:** `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`

Stacked on 03: its test project.

**Status:** ready-for-agent

- [ ] The recording-fake tests pin these native-call counts:
  - the first build;
  - a no-change rebuild: zero;
  - a text change: one `SetText`;
  - a class token added and removed;
  - one style declaration changed and one dropped;
  - a keyed row appended (create plus its attributes), removed (one `Remove`), and two rows reversed (one `Reorder`);
  - an `@if` element appearing and disappearing;
  - a folded block built once and moved;
  - loose text in a panel giving a generated label;
  - a child's parameter change;
  - `@ref` set, then cleared on removal.
- [ ] Indices stay correct while a removed element is still outgoing: the fake keeps it among its parent's children.
- [ ] A missing-`@using` fixture logs the unrecognised-directive warning once.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
