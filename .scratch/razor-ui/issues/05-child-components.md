# 05: Child components

**What to build:** Razor components compose. An uppercase tag places a child component with parameters and child content, the child rebuilds only when its parameters changed, and `@ref` captures elements and components (spec: "The builder" child components and references, "Components, mounting and the dispatcher"; ADR-0067, ADR-0068).

- `OpenComponent<T>` finds or makes the child at its key, in its own region under the current element.
- **Parameters.** Parameters are set by name through setters compiled once per type from its `[Parameter]` properties, and cached in the runtime. `Release` clears the cache.
- **Rebuilds.** A child rebuilds when a parameter changed by `Equals`; a `RenderFragment` always counts as changed. Parents build first. `OnParametersSet` runs before a rebuild caused by parameters.
- `ChildContent` and other `RenderFragment` slots run in regions of their own.
- **`@ref`.** On an element it gives an `ElementId` through `ElementReference`, invoked again with none when the element is removed. On a component it gives the instance.

**Blocked by:** 04

**Touches:** `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/Tests/`, `packages/FenrirUI/README.md`

Stacked on 04.

**Status:** ready-for-agent

- [ ] Fake-seam tests:
  - a child's parameter change costs only the child's changed values, and an unchanged parameter rebuilds nothing;
  - a `ChildContent` fragment's elements keep their keys across parent rebuilds;
  - `@ref` is set, then cleared on removal.
- [ ] A scripting test: a two-level tree with a frame component wrapping `ChildContent` appears, and a parent field passed as a parameter updates the child's text the same frame.
- [ ] After a reload the setter cache holds nothing of the old generation (the unload check passes).
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
