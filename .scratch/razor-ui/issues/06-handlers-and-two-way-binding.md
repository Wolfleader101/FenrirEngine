# 06: Handlers and two-way binding

**What to build:** `@onclick` and the other handlers run from the frame's UI events, and `@bind` keeps a field and a value control in step both ways (spec: "Handlers and binding"; ADR-0069, amending ADR-0065).

- The builder records handlers per entry and kind. In the build stage, before rebuilds, the dispatcher reads `EventReader<UIEvent>`. It walks from each event's target through `GetParent` to the nearest handler of that kind on that root, runs it, and marks its component for rebuild. Handlers are `Action` or `Action<UIEvent>`. A handler that throws is logged, and its component is disabled as for a throwing build.
- **The debug warning.** A pointer handler on an element whose resolved style takes no pointer events logs once, after its first layout. If no native reads that, add the smallest generated read (a resolved `pointer-events`).
- **`@bind`** on `slider`, `progress`, `checkbox` and `textentry`.
  - The value is pushed only when it changed.
  - On `Change`, the dispatcher reads `GetValue` or `GetEntryText` into the field through the generated setter, then runs `@bind:after`, then rebuilds.
  - `@bind-X` on component parameters works through Razor's own lowering.

**Blocked by:** 05

**Touches:** `packages/FenrirUI/Managed/Microsoft.AspNetCore.Components/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/README.md`

Stacked on 05.

**Status:** ready-for-agent

- [ ] Scripting tests with the input test feed:
  - a click on a button's label runs the button's `@onclick` once, and the component's text shows the change the same frame;
  - an `Action<UIEvent>` handler receives the event's position and kind;
  - a handler on an ancestor runs when nothing nearer handles the kind;
  - a slider dragged by the feed writes the bound field, and `@bind:after` runs once;
  - a field changed by code moves the slider, and sends no `Change` back;
  - a text entry binds both ways;
  - a component's `@bind-Value` parameter round-trips through its parent.
- [ ] The debug warning logs once for `@onclick` on a panel without `pointer-events: all`, and not for a button.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
