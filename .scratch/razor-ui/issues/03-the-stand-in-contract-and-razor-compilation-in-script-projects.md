# 03: The stand-in contract and `.razor` compilation in script projects

**What to build:** a script project that imports `Fenrir.UI.Razor.props` compiles `.razor` files with the stock SDK's Razor generator against the engine's stand-in `Microsoft.AspNetCore.Components`, with no ASP.NET Core reference (spec: "The stand-in contract", "`Fenrir.UI.Razor.props` and the generators"; ADR-0067).

- The project `packages/FenrirUI/Managed/Microsoft.AspNetCore.Components/` holds the stand-in API from the research note's section 1.4, trimmed to what the generated code calls (spec list). Its `EventHandlers` lists ADR-0069's handler names, and its `BindAttributes` lists `slider`, `progress`, `checkbox` and `textentry`. Record whether `[EventHandler]` accepts `UIEvent` (a struct) as its argument type, or whether a `UIEventArgs` class wraps it.
- The project `packages/FenrirUI/Managed/Fenrir.UI.Razor/` is created with `UIComponent : ComponentBase` and nothing more yet. It is a contract project like `Fenrir.UI`.
- `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, imported after the scripting SDK's props. When the project has `.razor` files, it:
  - finds the running SDK's Razor generator from MSBuild properties, never a hard-coded version;
  - adds `.razor` files as `AdditionalFiles`;
  - passes `RazorLangVersion` and `RootNamespace` as compiler-visible properties;
  - references the two contracts, compile-time only;
  - supplies the default imports and `@inherits UIComponent`.

  An in-repo editor path, as `Fenrir.Scripting.InRepo.props` has, references the projects. A project without `.razor` files is unchanged.
- A new `packages/FenrirUI/Managed/CMakeLists.txt`, added from `packages/FenrirUI/CMakeLists.txt` when scripting is on, builds both new projects as contracts (through `fenrir_scripting_contract` or its building blocks in `FenrirScriptingExtension.cmake`, extended if it cannot build a contract with no natives) and puts the props beside them. `fenrir_add_demo_app` passes their folders as it passes the other contracts'. Sharing them with scripts at run time is ticket 05's, through the UI extension.
- The managed test project `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/` (prior art: `packages/Iris/Tools/Iris.Bindings/tests`), run by CTest, imports the props, so its `.razor` files compile.

**Blocked by:** none

**Touches:** `packages/FenrirUI/Managed/Microsoft.AspNetCore.Components/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Managed/CMakeLists.txt`, `packages/FenrirUI/CMakeLists.txt`, `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`, `examples/DemoScaffold/cmake/FenrirDemoApp.cmake`

**Status:** ready-for-agent

- [ ] The test project's fixtures compile and their generated `BuildRenderTree` runs against a recording `RenderTreeBuilder` in a test. The fixtures cover an element with attributes, text with an expression, `@if`, `@foreach` with `@key`, `@onclick`, `@bind` on a slider, a child component with a `[Parameter]` and `ChildContent`, `@ref`, a `<style>` block and `@code`. The recorded call sequence matches the research note's listing.
- [ ] No assembly named `Microsoft.AspNetCore.*` other than the stand-in is referenced, and the build output holds no ASP.NET Core framework reference.
- [ ] A script project without `.razor` files builds byte-for-byte as before the props were imported (the same generated sources).
- [ ] The build time of the test project, clean and incremental, is recorded in the PR.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
