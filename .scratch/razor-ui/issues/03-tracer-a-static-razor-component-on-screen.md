# 03: Tracer: a static Razor component on screen

**What to build:** a `.razor` file in a script project compiles with the stock SDK's Razor generator against the engine's stand-in. `root.Mount<T>(parent)` then shows its elements, classes, inline style and expression text on the UI root. A script reload removes it, and the old generation is collected. This is the thinnest path through every layer (spec: "The stand-in contract", "`Fenrir.UI.Razor.props` and the generators", "The builder", "Components, mounting and the dispatcher"; ADR-0067, ADR-0068).

- **The stand-in** `Microsoft.AspNetCore.Components`: the members the generated code of a static component calls (the research note's section 1.4, trimmed). `EventHandlers` and `BindAttributes` wait for ticket 06.
- **`Fenrir.UI.Razor`**:
  - `UIComponent : ComponentBase` with `Root`, `Element` and `Context`;
  - `root.Mount<T>(parent, index, init)` returning `ComponentRef<T>` with `Unmount`, where mounting makes a host panel with the readable class;
  - `RazorRuntime` and a minimal `ElementBuilder` over the element seam. On the first build, it creates lowercase tags through `CreateElement` and applies `class`, `style`, `id` and other attributes and the text runs, a `label`'s or `button`'s text through `SetText`. Later builds and keys are ticket 04's.
- **`Fenrir.UI.Razor.props`**: when the project has `.razor` files, it adds the running SDK's Razor generator (found from MSBuild properties, never a hard-coded version), `.razor` files as `AdditionalFiles`, `RazorLangVersion` and `RootNamespace` as compiler-visible properties, the contracts compile-time only, the default imports with `@inherits UIComponent`, and the shipped `RazorDispatcher.cs`. It has an in-repo editor path like `Fenrir.Scripting.InRepo.props`. A project without `.razor` files is unchanged.
- **`RazorDispatcher.cs`**: a `[StageSystem(UIStages.Build)]` that builds queued mounts. Its `OnDestroy` calls `RazorRuntime.Release()`, which removes every host immediately and clears every registry.
- **The build**: the two contracts are built as contracts, the props are installed beside them, and demo apps get their folders. FenrirUI's scripting extension shares them after `Fenrir.UI`. The UI scripting fixture imports the props.
- **The managed test project** `Fenrir.UI.Razor.Tests` (prior art: `Iris.Bindings`' tests), run by CTest, with a recording fake of the element seam.

**Blocked by:** 01

**Touches:** `packages/FenrirUI/Managed/Microsoft.AspNetCore.Components/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Managed/CMakeLists.txt`, `packages/FenrirUI/CMakeLists.txt`, `packages/FenrirUI/Scripting/`, `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`, `examples/DemoScaffold/cmake/FenrirDemoApp.cmake`

Cut from `razor-ui` once 01 has merged into it.

**Status:** resolved

- [x] A scripting test with a headless App: a `.razor` component mounted from `OnCreate` appears on the first frame, read from C++ as tags, classes, inline style and text. One of its texts comes from a field.
- [x] Reload: after a reload the old host is gone the same frame, the new generation mounts again, and the released generation is collected (the scripting tests' unload check).
- [x] A fake-seam test records the first build's native calls.
- [x] No `Microsoft.AspNetCore.*` assembly other than the stand-in is referenced, and a script project without `.razor` files generates the same sources as before.
- [x] The test project's build time, clean and incremental, is recorded in the PR.
- [x] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
