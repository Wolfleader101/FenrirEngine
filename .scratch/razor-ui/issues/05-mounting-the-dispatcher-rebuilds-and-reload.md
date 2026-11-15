# 05: Mounting, the dispatcher, rebuilds and reload

**What to build:** a script mounts a Razor component into a UI root's element. The component builds in the build stage and rebuilds when something says so. A script reload removes it and holds nothing of the old generation (spec: "Components, mounting and the dispatcher"; ADR-0068).

- `UIComponent`'s surface:
  - `Root`, `Element` and `Context`;
  - `StateHasChanged()` and `BuildHash()`;
  - the hooks `OnMount`, `OnParametersSet`, `OnBuilt(bool first)` and `OnUnmount`.

  `root.Mount<T>(parent, index, init)` returns `ComponentRef<T>`, with `Unmount(RemoveMode)`. Mounting creates the host panel, which gets the component's readable class.
- `RazorRuntime` keeps the components per root and drops a root's components when the root is destroyed. Rebuild triggers: a queued mount, `StateHasChanged`, a changed `BuildHash` (checked for each mounted component every frame), and changed parameters, by `Equals`, with a `RenderFragment` always changed (replacing ticket 04's always-rebuild for children). Parents build first, and each component builds at most once a frame.
- Shipped `RazorDispatcher.cs`, a `[StageSystem(UIStages.Build)]`, added to `Compile` by `Fenrir.UI.Razor.props`. Its `OnDestroy` calls `RazorRuntime.Release()`. Release removes every host immediately and clears every registry and cache: setters, overrides, parsed markup and stylesheets.
- A component whose build throws is logged with its type and disabled until the next reload. Its elements stay as they were.
- FenrirUI's scripting extension shares `Microsoft.AspNetCore.Components` and `Fenrir.UI.Razor` after `Fenrir.UI`. The UI scripting fixture imports `Fenrir.UI.Razor.props` and gains `.razor` components.
- FenrirUI's README gains a "Razor components" section: writing, mounting, rebuild rules and reload. The scripting README links to it.

**Blocked by:** 02, 04

**Touches:** `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/README.md`, `packages/FenrirScripting/README.md`

Cut from `razor-ui` once 02 and 04 have merged into it.

**Status:** ready-for-agent

- [ ] Scripting tests with a headless App:
  - a component mounted in `OnCreate` appears with its tree on the first frame's layout;
  - `StateHasChanged` from an `Update` system shows on the same frame;
  - a `BuildHash` that changes rebuilds and an unchanged one does not, both seen through a build counter the fixture exposes;
  - a parent's parameter change rebuilds the child, and an unchanged one does not;
  - `Unmount` removes the host with its outro;
  - destroying the root drops its components.
- [ ] Reload: after a reload the old hosts are gone the same frame, the new generation's `OnCreate` mounts again, and the released generation is collected (the scripting tests' unload check).
- [ ] A throwing build disables only its component, and a reload brings it back.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
