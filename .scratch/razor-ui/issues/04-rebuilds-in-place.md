# 04: Rebuilds in place

**What to build:** a mounted component rebuilds when it says so. Each rebuild makes, updates, moves and removes elements in place by sequence number, with no native call for a value that did not change (spec: "The builder", "Components, mounting and the dispatcher"; ADR-0067, ADR-0068).

- **Triggers.** `StateHasChanged()`, and `BuildHash()` compared each frame for every mounted component. Each component builds at most once a frame. Hooks: `OnMount`, `OnBuilt(bool first)` and `OnUnmount`.
- **Keys and regions.** Keys are `(sequence, occurrence)`, or `(sequence, key)` with `@key`. Each element is resolved at its first non-attribute call. A tag change at the same key replaces the element.
- **Values.** Class token diffs, style declaration diffs (a dropped declaration is reset with `""`), id, value by tag (`SetValue`, or `SetEntryText` for a `textentry`), other attributes through `SetAttribute`. Text is concatenated per element: `SetText` in a `label` or `button`, and a generated `label` elsewhere. Each native is called only on change.
- **Folded markup.** `MarkupFragment` parses `AddMarkupContent` strings once, covering elements, attributes, text, entities and void elements, into the same operations. A `<style>` is handed to a sheet hook that ticket 07 fills; until then it is a no-op. Whitespace-only runs are ignored.
- **Removal and order.** An unvisited entry is removed with `Remove(Outro)`. Moves use `Reorder`, outside the longest increasing subsequence only. Indices count children the builder does not own: outgoing elements, and children marked unowned for ticket 08.
- **Diagnostics.**
  - The unrecognised-directive warning, once per sequence.
  - A component whose build throws is logged with its type and disabled until the next reload, with its elements left as they were.
- FenrirUI's README gains a "Razor components" section: writing, mounting, rebuild rules and reload. The scripting README links to it.

**Blocked by:** 03

**Touches:** `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/Tests/`, `packages/FenrirUI/README.md`, `packages/FenrirScripting/README.md`

Stacked on 03.

**Status:** resolved

- [x] The fake-seam tests pin these native-call counts:
  - a no-change rebuild: zero;
  - a text change: one `SetText`;
  - a class token added and removed;
  - one style declaration changed and one dropped;
  - a keyed row appended, removed (one `Remove`), and two rows reversed (one `Reorder`);
  - an `@if` element appearing and disappearing;
  - a folded block built once and moved;
  - loose text in a panel giving a generated label.
- [x] Indices stay correct while a removed element is outgoing.
- [x] Scripting tests:
  - `StateHasChanged` from an `Update` system shows the same frame;
  - a `BuildHash` that changes rebuilds and an unchanged one does not, seen through a build counter;
  - `Unmount` removes with the outro;
  - a throwing build disables only its component, and a reload brings it back.
- [x] A missing-`@using` fixture logs the unrecognised-directive warning once.
- [x] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
