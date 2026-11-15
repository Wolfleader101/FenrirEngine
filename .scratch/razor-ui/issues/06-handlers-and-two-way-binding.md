# 06: Handlers and two-way binding

**What to build:** `@onclick` and the other handlers run from the frame's UI events, and the change shows the same frame. `@bind` keeps a field and a value control in step both ways, and `@bind-X` binds a component's parameter (spec: "Handlers and binding"; ADR-0069, amending ADR-0065).

- **The stand-in's discovery classes.**
  - `EventHandlers` holds one `[EventHandler]` per ADR-0069 name. Record whether it accepts `UIEvent` (a struct) as its argument type, or whether a `UIEventArgs` class wraps it.
  - `BindAttributes` covers `slider`, `progress`, `checkbox` and `textentry`.
- **Dispatch.** The builder records handlers per entry and kind. In the build stage, before rebuilds, the dispatcher reads `EventReader<UIEvent>`. It walks from each event's target through `GetParent` to the nearest handler of that kind on that root, runs it, and marks its component for rebuild. Handlers are `Action` or `Action<UIEvent>`. A throwing handler disables its component as a throwing build does.
- **The pointer warning.** A pointer handler on an element whose resolved style takes no pointer events logs once in debug builds, after the element's first layout. If no native reads that, add the smallest generated read.
- **`@bind`.** The value is pushed only when it changed. On `Change`, the dispatcher reads `GetValue` or `GetEntryText` into the field through the generated setter, runs `@bind:after`, and rebuilds. `@bind-X` on a child component works through Razor's own lowering.

**Blocked by:** 04, 05

**Touches:** `packages/FenrirUI/Managed/Microsoft.AspNetCore.Components/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/README.md`

Stacked on 05.

**Status:** ready-for-agent

- [ ] Scripting tests with the input test feed:
  - a click on a button's label runs the button's `@onclick` once, and the component's text shows the change the same frame;
  - an `Action<UIEvent>` handler receives the event's position and kind;
  - an ancestor's handler runs when nothing nearer handles the kind;
  - a slider dragged by the feed writes the bound field, and `@bind:after` runs once;
  - a field changed by code moves the slider and sends no `Change` back;
  - a text entry binds both ways;
  - a child's `@bind-Value` round-trips through its parent.
- [ ] The debug warning logs once for `@onclick` on a panel without `pointer-events: all`, and not for a button.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
