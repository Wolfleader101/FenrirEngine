Status: ready-for-agent

# Razor UI: `.razor` components over FenrirUI, compiled by the stock SDK and built in place, with UIDemo

Origin: the user wants s&box's feel for game UI in C#, where `.razor` panels are a convenience over plain panels. ADR-0061 and ADR-0065 left "a Razor-style syntax ... a C# source generator over the same element API" and any binding language to a later effort; this is that effort. Research on 2026-11-14 (`research/razor-compilation-and-builder.md`, building on `.scratch/fenrir-ui/research/sbox-ui-stack.md`, which git history keeps once FenrirUI's scratch folder goes) settled the facts. A grilling session on 2026-11-15 settled the decisions, recorded in these ADRs, which this spec cites rather than restates:

| ADR | Decision |
|---|---|
| 0067 | Razor components compile with the stock SDK generator against an engine stand-in, and build FenrirUI elements in place |
| 0068 | A Razor component is a managed object mounted into an element, and one dispatcher runs every component in a UI build stage |
| 0069 | Razor handlers and two-way binding ride the UI event queue |
| 0070 | Razor component stylesheets are scoped by a scope class the native stylesheet loader applies |

They build on ADR-0007 (ECS-first, behaviours as a later layer), ADR-0004 and ADR-0009 (shared contracts, extensions, reload of script assemblies), ADR-0039 (extension stages), ADR-0042 (generated bindings) and the FenrirUI ADRs 0061 to 0066. They amend ADR-0007, 0061, 0064 and 0065.

## Problem Statement

A C# game developer can build any FenrirUI tree today, but only call by call. UIDemo's speedometer takes eleven `SetStyle` calls and two `CreateElement`s to make a panel and a label. Its owner looks up the element by id every frame to change one string, and reads clicks by walking parents from a `UIEvent`'s target to find which button it was. A settings screen with ten sliders is a hundred lines of plumbing: make each element, remember its id, push each value when it changes, read each `Change` back, and remove and remake list rows by hand when the list changes. Nothing ties a piece of UI to its state, its styles or its handlers. There is no way to write the shape of a screen once, with its loops, conditions and bindings, and have the engine keep the element tree in step. A web developer would write that in Blazor, and an s&box developer in a `.razor` panel.

## Solution

`.razor` files in a game's script project compile, with the stock .NET SDK's Razor source generator, into Razor components: C# classes over FenrirUI's existing C# API, with no virtual DOM (ADR-0067).

- **Compiling.** `Fenrir.UI.Razor.props`, which FenrirUI ships, wires the SDK's generator into an ordinary script project. The generated code compiles against `Microsoft.AspNetCore.Components`, an engine stand-in holding the few dozen names it calls, and against the runtime contract `Fenrir.UI.Razor`.
- **Building in place.** A builder keyed by Razor's compile-time sequence numbers makes, updates and removes real FenrirUI elements as the code runs, and makes no native call for a value that did not change.
- **State.** A mounted Razor component is one managed object holding UI-local state, mounted into any element of any UI root, including a slot in a C++ markup document.
- **The dispatcher.** One dispatcher system runs every component in a new FenrirUI hosted stage, `Fenrir.UI.Build`, before the frame's styling (ADR-0068). It runs handlers for the frame's UI events, rebuilds what asked to be rebuilt (`StateHasChanged`, a handler, changed parameters or a changed `BuildHash`), and calls paint overrides in `Fenrir.UI.Paint`.
- **Handlers and binding.** `@onclick` and the other handlers, and `@bind` on value controls, ride the UI event queue (ADR-0069).
- **Styles.** `Foo.razor.css` and `<style>` blocks are scoped to their component by a scope class, which FenrirUI's stylesheet loader adds to every rule natively, with `::deep` to reach children (ADR-0070).
- **Reload.** A script reload drops every component and its elements, and the new generation mounts again.

UIDemo shows two things. Its speedometer becomes one `.razor` file. A Razor settings page mounts into the C++ pause menu, binding sliders and checkboxes, listing keyed rows, placing a markup document and hot-reloading its scoped stylesheet.

## User Stories

### Writing a component

1. As a C# game developer, I want to write a piece of UI as one `.razor` file of FenrirUI tags with embedded C#, so that the shape of a screen reads as markup instead of a hundred element calls.
2. As a C# game developer, I want `@if`, `@foreach` and early `return` in markup, so that conditional and repeated UI is ordinary C#.
3. As a C# game developer, I want text with expressions (`<label>HP: @Health</label>`) to set the element's text, so that a readout is one line.
4. As a C# game developer, I want loose text inside a panel to become a label, so that I don't have to wrap every word in a tag.
5. As a C# game developer, I want `class="slot @(selected ? "selected" : "")"` and `style="width: @(Fill)%"` to change only the classes and declarations that changed, so that dynamic styling costs nothing when nothing changes.
6. As a C# game developer, I want lowercase tags to be FenrirUI's elements (`panel`, `label`, `button`, `slider`, `checkbox`, `textentry`, any custom C++ tag) and unknown lowercase tags to be plain panels, so that what I know from markup documents carries over.
7. As a C# game developer, I want uppercase tags to be other Razor components, with `[Parameter]` properties as attributes, so that UI composes.
8. As a C# game developer, I want `ChildContent` and other `RenderFragment` slots, so that a frame or a dialog component can wrap content its user provides.
9. As a C# game developer, I want `@key` on repeated elements, so that a list keeps each row's element, focus and transitions when rows are inserted, removed or reordered.
10. As a C# game developer, I want `@ref` to capture an element's id or a child component, so that I can focus an element or call into a child.
11. As a C# game developer, I want my editor to complete tags, parameters and directives in `.razor` files, so that writing UI is as fast as writing C#.

### State and rebuilds

12. As a C# game developer, I want `@code` fields to hold a component's UI-local state, such as the open tab or a typed filter, so that small UI state needs no ECS component.
13. As a C# game developer, I want to read game state from the ECS through `Context` while building, so that the UI shows the world's truth.
14. As a C# game developer, I want a click handler's changes to show without calling anything, so that the common case just works.
15. As a C# game developer, I want `StateHasChanged()` to rebuild a component on demand, so that state changed elsewhere can be shown.
16. As a C# game developer, I want to return a hash of what a HUD shows from `BuildHash()`, so that it rebuilds only when health or ammo change.
17. As a C# game developer, I want a child to rebuild when its parameters change, and not otherwise, so that a large screen rebuilds only what changed.
18. As a C# game developer, I want `OnMount`, `OnParametersSet`, `OnBuilt(first)` and `OnUnmount` hooks, so that I can set up and tear down around the tree.
19. As a C# game developer, I want hover, press and focus styling to need no rebuild, so that it stays a stylesheet's job.

### Mounting and documents

20. As a C# game developer, I want to mount a component into any element of any UI root (`root.Mount<Inventory>(parent)`) and unmount it with its outro, so that Razor UI lives on the overlay, in images and in the world alike.
21. As a UI developer, I want to leave a slot element in a markup document that C# mounts a component into, so that a C++ screen can host a Razor part.
22. As a C# game developer, I want `<Document Src="ui/credits.ui" />` to place a markup document inside a component, so that static screens written as documents can be reused.
23. As a C++ game developer, I want Razor-built elements to be ordinary elements, so that C++ can find, read and style them.

### Events and binding

24. As a C# game developer, I want `@onclick`, `@onchange`, `@onsubmit`, `@oncancel`, the pointer, key, text and focus handlers, as `Action` or `Action<UIEvent>`, so that reacting to UI is a lambda on the element.
25. As a C# game developer, I want a click on a label inside a button to reach the button's `@onclick`, so that handlers sit where they make sense.
26. As a C# game developer, I want a debug warning when I put a pointer handler on an element that takes no pointer events, so that the opt-in rule doesn't cost me an hour.
27. As a C# game developer, I want `@bind="Volume"` on a slider, checkbox or text entry to keep a field and a control in step both ways, with `@bind:after` to run code after a change, so that a settings screen is one attribute per control.
28. As a C# game developer, I want `@bind-Value` on my own components' parameters, so that a custom control binds like a built-in one.

### Styles

29. As a C# game developer, I want `Inventory.razor.css` beside `Inventory.razor` to load automatically, so that a component carries its styles.
30. As a C# game developer, I want a component's styles to reach only its own elements, with `::deep` to reach into children, so that two components' `.title` never collide.
31. As a C# game developer, I want to edit a `.razor.css` and see it change without rebuilding, so that styling is as quick as with stylesheet files.
32. As a C# game developer, I want `<style>` blocks inside a `.razor` file, scoped the same way, so that a small component is one file.
33. As a UI developer, I want each component's root element to carry a readable class named after it, so that a game's own sheets can theme it.

### Painting

34. As a C# game developer, I want to override `OnPaint` on a component to paint into its root element through the Painter facade, so that a gauge is one file with no separate system.

### Reload and the engine

35. As a C# game developer, I want a script reload to bring my Razor UI back from my mounting code, so that iterating on a screen is a rebuild.
36. As an engine developer, I want the Razor runtime to hold nothing from a released script generation, so that reloads never leak a load context.
37. As an engine developer, I want FenrirUI's C++ to know nothing about Razor, so that the UI stays complete without C# (ADR-0061).
38. As a game developer, I want a project with no `.razor` files to build exactly as before, so that Razor is opt-in.
39. As a game developer, I want UIDemo to show Razor driving the HUD and a settings page inside the C++ menu, so that I can copy a working example.

## Implementation Decisions

### FenrirUI: scoped stylesheets (ADR-0070)

- `UIRoot::LoadStylesheet(path, scope)` and `UIRoot::AddStylesheet(text, name, scope)` take an optional scope class name. Empty means unscoped, as today.
- `Stylesheet::Parse` takes the scope. After nesting is resolved, it appends `.scope` to the rightmost compound selector of every rule (`.a .b` becomes `.a .b.s-x`, `.a > label` becomes `.a > label.s-x`).
- `::deep` is a combinator marker, legal only in a scoped sheet: `.a ::deep .b` puts the scope on `.a` and drops the marker, leaving a descendant combinator. An unscoped sheet that uses `::deep` logs an error for that rule.
- `@keyframes` names and `@font-face` are not scoped. `@import`ed sheets inherit the scope.
- Hot reload keeps each file's scope. A sheet named twice with the same scope is added once (dedup by path or name plus scope).
- The C# binding gains `LoadStylesheet` with a scope and `AddStylesheet(root, text, name, scope)`. Both are generated, as ticket 20's natives are.

### FenrirUI: the build stage (ADR-0068)

- `UIRoots::AddBuildStage(order, work)` and `RemoveBuildStage(handle)`, shaped like the paint stage. They run once a frame as the first work of the UI's `PostUpdate`, before any root advances transitions or resolves styles, lowest order first.
- The scripting extension declares `Fenrir.UI.Build` (`UIStages.Build`). Structural changes its systems record play back when the stage ends, as for `Fenrir.UI.Paint`.
- Elements made in the build stage are styled, laid out and painted in the same frame.

### The stand-in contract: `Microsoft.AspNetCore.Components` (ADR-0067)

- A managed project at `packages/FenrirUI/Managed/Microsoft.AspNetCore.Components/`, with no engine dependency, built as a contract and shared by the UI extension. It holds exactly what the research note's "Builder members the generated code needs" and its stand-in listing name:
  - `IComponent` and `ComponentBase` with a protected virtual `BuildRenderTree(RenderTreeBuilder)`;
  - an abstract `Rendering.RenderTreeBuilder` with every member the generated code calls (`OpenElement`, `CloseElement`, `OpenComponent<T>`, `CloseComponent`, `AddMarkupContent`, the `AddContent` and `AddAttribute` overloads, `AddComponentParameter`, `AddMultipleAttributes`, `SetKey`, `SetUpdatesAttributeName`, the reference captures, the stop-propagation and prevent-default attributes);
  - `RenderFragment` and `RenderFragment<T>`;
  - `EventCallback`, `EventCallback<T>` and `EventCallbackFactory`, with the binder extensions;
  - `ElementReference`, `BindConverter` and `ParameterAttribute`;
  - `CompilerServices.RuntimeHelpers`.

  It also holds two discovery classes:
  - **`EventHandlers`**, with an `[EventHandler]` per ADR-0069 name, each naming the handler's argument type. That is `Fenrir.UI`'s `UIEvent` when the generator accepts a struct there; the stand-in may reference the `Fenrir.UI` contract for it. Otherwise it is a small `UIEventArgs` class wrapping one, and `Action<UIEvent>` handlers are adapted through `EventCallback.Factory`'s overloads. Ticket 06 records which.
  - **`BindAttributes`**, with `[BindElement]` for `slider`, `progress`, `checkbox` and `textentry`, binding the value attribute and the `onchange` event.
- Its version is part of the contract, as for every contract. Its namespace layout follows Blazor's, so generated code resolves unchanged.

### `Fenrir.UI.Razor.props` and the generators (ADR-0067, ADR-0070)

- `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, shipped in the scripting SDK folder's UI part, imported by a script project after `Fenrir.Scripting.props`. When the project has `**/*.razor`, it:
  - adds the running SDK's Razor source generator as an `Analyzer`, found from the SDK's own `Microsoft.NET.Sdk.Razor` folder: `$(NETCoreSdkVersion)` and `$(MSBuildExtensionsPath)`, never a hard-coded version;
  - adds `.razor` files as `AdditionalFiles`, and `RazorLangVersion` and `RootNamespace` as `CompilerVisibleProperty`;
  - references the stand-in and `Fenrir.UI.Razor`, compile-time only;
  - adds a default `_Imports.razor` content: `@using Fenrir.UI`, `@using Fenrir.UI.Razor`, `@using Microsoft.AspNetCore.Components.Web` (where `EventHandlers` lives) and `@inherits UIComponent`;
  - adds the shipped `RazorDispatcher.cs` to `Compile`;
  - adds FenrirUI's Razor generator (`Fenrir.UI.Razor.Generators`) as an `Analyzer`, and `.razor.css` files as `AdditionalFiles`.

  A project without `.razor` files gets none of it.
- The in-repo editor path mirrors `Fenrir.Scripting.InRepo.props`: when no SDK folder is passed, the contracts come from their source projects.
- `Fenrir.UI.Razor.Generators` emits, for each `Foo.razor.css`, a partial `Foo` carrying `[ComponentStylesheet(path, text)]`. The path is absolute at build time. `Foo`'s name is resolved as Razor resolves it: `RootNamespace` plus folder, or an `@namespace` in the file or its nearest `_Imports.razor`. It warns when a `.razor.css` has no matching `.razor`.

### The builder (ADR-0067)

- `Fenrir.UI.Razor`'s `ElementBuilder : RenderTreeBuilder` works through an internal **element seam**, a small interface over the `UIRoot` natives it uses: create, remove, reorder, children, classes, style, id, text, attribute, value, entry text, load document, add class. The production implementation is `UIRoot`. Tests use a recording fake.
- **Retained state**:
  - per mounted component, a tree of regions;
  - per region, its entries keyed by `(sequence, occurrence)` or `(sequence, key)`;
  - per entry, its `ElementId`, its tag, its last class tokens, its style declarations, its text and its attribute values, the build number that last visited it, and its handlers by kind.

  A child component's entry holds the child object and its region.
- **Resolution.** `OpenElement` buffers the tag and attributes. The element is resolved, found by key or created at the end of its parent, at the first non-attribute call: `SetKey`, content, a child, or `CloseElement`. A tag change at the same key removes the old element and makes a new one.
- **Applying values:**
  - **Attributes.** `class` splits into tokens and diffs them (`AddClass`/`RemoveClass`). `style` splits into declarations and diffs them (`SetStyle`; a dropped declaration is reset by `SetStyle(name, "")`, which ticket 02 confirms or adds as a reset). `id` uses `SetIdString`. `value` uses `SetValue`, or `SetEntryText` for a `textentry`, chosen by tag. Any other attribute uses `SetAttribute`. Each native is called only when the value changed.
  - **Text.** Content runs within one element are concatenated and pushed with one `SetText` when the result changed. That applies in a `label` or `button`. In any other element, a run between child elements becomes a generated `label` child, keyed by the run's first sequence number.
  - **Folded markup.** `AddMarkupContent` strings are parsed by `MarkupFragment`, a C# parser for the well-formed HTML subset Razor's encoder emits: elements, attributes, text, the entities Razor emits, void elements and `<style>`. Each string is parsed once and cached per string instance. Its elements become entries keyed `(sequence, occurrence, child path)`, through the same apply rules. Whitespace-only runs are ignored.
  - **`<style>`.** A `<style>` element, folded or not, adds its text as a scoped sheet once per root and component type (ticket 07). It makes no element.
- **Removal and order.** At `CloseElement`, and at the end of a region, any entry not visited this build is removed (`Remove(Outro)`) and forgotten. Its id is still counted among its parent's children while it goes. Visited children whose native order differs from the visited order are moved with `Reorder`, only for those outside the longest increasing subsequence of their old positions. Indices account for children the builder does not own, such as outgoing elements and a placed document's elements.
- **Child components.** `OpenComponent<T>` finds or creates the child at its key. `AddComponentParameter` and parameter `AddAttribute`s collect the parameters, which are applied at `CloseComponent` through setters compiled once per type and cached in the runtime. The child rebuilds when ADR-0068 says it does, in its own region under the current element. `ChildContent` and other fragments run in a region of their own.
- **References.** `@ref` on an element hands the action an `ElementReference` that wraps the `ElementId`, and `UIComponent` converts it to an `ElementId`. When the entry is removed, the builder invokes it again with none. `@ref` on a component hands the instance.
- **Unowned children.** The builder lets an element hold children it does not own: it never visits, updates or removes them, but counts them in indices. Ticket 08's `<Document Src>`, a built-in Razor component in `Fenrir.UI.Razor`, uses this. It makes a host `panel` and calls `LoadDocument` into it once, and again when `Src` changes, after removing the old content.
- **Errors.** An attribute name starting `@`, or `on` followed by a known event, reaching `SetAttribute` means a directive was not recognised. It is logged once per sequence and names the component and the likely missing `@using`.

### Components, mounting and the dispatcher (ADR-0068)

- `UIComponent : ComponentBase` is the default base. Its members are:
  - `Root`, `Element` (its root element, the host it was mounted into, owned by the mounter) and `Context`;
  - `StateHasChanged()` and the overridable `BuildHash()`;
  - the hooks `OnMount`, `OnParametersSet`, `OnBuilt(bool first)` and `OnUnmount`;
  - `OnPaint(PainterId, ElementBoxes)`.
- **Mounting.** `root.Mount<T>(parent, index = -1, Action<T>? init = null)` is an extension on `UIRoot` in `Fenrir.UI.Razor`. It creates a host `panel` under `parent` carrying the component's readable class and scope class, constructs `T`, runs `init` (the place to set parameters), and queues the first build in the next build stage. It returns `ComponentRef<T>` (`Instance`, `Host`, `IsMounted`, `Unmount(RemoveMode)`). Mounting from outside a stage is allowed: the component is queued.
- **The registry.** `RazorRuntime` is static in `Fenrir.UI.Razor` and keeps the mounted components per root. It drops a root's components when the root is destroyed.
- **`RazorDispatcher.cs`** is shipped source, compiled into the script. It is one `[StageSystem(UIStages.Build)]` and one `[StageSystem(UIStages.Paint)]` system, each a single call into `RazorRuntime` with its `SystemContext`. In the build stage:
  1. read `EventReader<UIEvent>` and dispatch (ADR-0069);
  2. collect the components to rebuild: queued mounts, `StateHasChanged`, handler-touched, and a changed `BuildHash` (checked for every mounted component);
  3. build them parents first, each once per frame, with children rebuilt by parameter change inside their parent's build.

  In the paint stage, it calls `OnPaint` for each mounted component whose type overrides it (found once per type), with `PaintInto(Element)`.
- **Reload.** The dispatcher's `OnDestroy` calls `RazorRuntime.Release()`. That removes every mounted host with `RemoveMode.Immediate`, clears the registry, the per-type setter, override and stylesheet caches, and the parsed-markup cache, and leaves no static reference to a generation type. A test checks that the released generation is collected.
- **Exceptions.** A component's build or handler that throws is logged with the component's type, and that component is disabled (not built again) until the next reload, as a throwing stage system is. Its elements stay as they were.

### Handlers and binding (ADR-0069)

- Each handler attribute (`EventCallback` or `EventCallback<UIEventArgs>`) is recorded on its entry by kind. Dispatch walks `GetParent` from `UIEvent.Target` to the nearest entry, of any component on that root, with a handler of the event's kind, invokes it with the event, and marks its component for rebuild.
- **The debug warning.** For a pointer-kind handler, in debug builds, the builder checks after the first layout whether the element's resolved `pointer-events` takes the pointer. This needs one native read, `GetPointerEvents` or a resolved-style query, which ticket 06 adds through the generated binding. If it doesn't, the builder warns once.
- **`@bind`** compiles, through `BindAttributes` and `BindConverter`, to a value attribute plus an `onchange` callback.
  - The builder pushes the value when it changed.
  - On a `Change` event, the dispatcher reads the control's value (`GetValue` or `GetEntryText`) and calls the generated setter. Then it runs `@bind:after`, then rebuilds.
  - For a checkbox, the value is `GetValue != 0`.
- `@bind-X` on a component is Razor's own lowering to `X` and `XChanged`, needing nothing more.

### Scoped component stylesheets (ADR-0070)

- **Scope class.** `s-` followed by eight hex digits of FNV-1a over the component type's full name. It is computed in C#, and a test pins the value for a known name.
- **Classes.** The builder adds the scope class to every element it creates, generated labels and folded-markup elements included, but not a placed document's elements. The host element gets the readable class: kebab case of the type name, without generic arity.
- **Loading.** On the first mount of a type into a root, the runtime loads the type's `[ComponentStylesheet]` with its scope: by path when the file exists, through `LoadStylesheet(path, scope)`, so hot reload applies; otherwise through `AddStylesheet(text, name, scope)`. `<style>` blocks load once per root and type, named by type and sequence.

### UIDemo

- **`Speedometer.razor`** replaces `Speedometer.cs`. It is a `panel` with a readout label bound to the smoothed speed, styled by `Speedometer.razor.css` in place of the eleven inline styles, with `BuildHash` over the speed rounded to the readout's precision and `OnPaint` drawing the gauge. A setup system mounts it on the HUD root from `OnCreate`, and again when the root has lost it.
- **`Settings.razor`** mounts from C# into a `razor-settings` slot element added to the pause menu's markup. It shows:
  - `@bind` on a volume slider, a subtitles checkbox and a player-name text entry, held in a C# settings component of the demo's;
  - a keyed `@foreach` list of key bindings with add and remove buttons (`@onclick`);
  - `<Document Src>` placing a small credits document;
  - a child component with `ChildContent`;
  - a scoped `Settings.razor.css` using `::deep`, which hot-reloads.
- `MenuClicks.cs` stays: the C++ menu's own buttons are still read through `EventReader<UIEvent>`, beside the Razor page's handlers.
- The demo's README says what the Razor variant shows and how to edit it live.

## Testing Decisions

A good test drives a public seam and asserts on what comes out: elements and their classes, styles, text and order, native-call counts, events handled, and fields written. The seams, highest first:

1. **The scripting harness with a headless App** (prior art: `packages/FenrirUI/Scripting/Tests`, which loads `Fenrir.UIScriptFixture` and ticks the App with FenrirInput's test feed). The fixture project imports `Fenrir.UI.Razor.props` and holds `.razor` components. Tests mount them and tick frames, and then:
   - read back the tree from C++ (tags, classes, text, order, rectangles);
   - feed clicks and value changes, and read fields through a fixture native or a logged value;
   - edit a `.razor.css` written to a temporary directory and see the resolved style change;
   - release the generation and check that the hosts are gone and the generation is collected.
2. **The builder against a recording fake element seam**, in a managed test project, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, run by CTest like the other managed checks (prior art: `packages/Iris/Tools/Iris.Bindings/tests`). Its components are real `.razor` files compiled by the same props. These tests pin native-call counts: the first build, a no-change rebuild (zero), a text change, a class token, a style declaration, a keyed insert, remove and reverse, an `@if` appearing and disappearing, a folded block, generated labels, a child's parameter change, and `@ref` set and cleared.
3. **FenrirUI's C++ stylesheet tests** (prior art: FenrirUI ticket 08's cascade tests and FenrirUI ticket 16's hot reload tests): scoped rules land on the rightmost compound, `::deep`, nesting, `@import` with a scope, `::deep` in an unscoped sheet as an error, dedup, and hot reload keeping the scope.
4. **A compile test**: a fixture project with `.razor` files builds against the stand-in alone, with no ASP.NET Core reference. A second fixture with a missing `@using` builds and logs the unrecognised-directive warning at mount.

No golden images; UIDemo is the visual check. Build and test under the msvc-x64-debug preset. CI runs what `.github/workflows/build-and-test.yml` runs; read it before claiming coverage.

## Out of Scope

- A forked or vendored Razor compiler, s&box's `Name:bind` syntax, uppercase tags as non-component classes (ADR-0067).
- Generic components (`@typeparam`), cascading values, `@inject` and dependency injection, `@page` routing, `@layout`, async lifecycle and async handlers.
- `@bind:event`, `@bind:get`/`@bind:set`, format strings and cultures.
- Keeping UI-local state or elements across a script reload. Re-adopting elements by key is the recorded upgrade path (ADR-0068).
- Metadata-update hot reload (`dotnet watch`, Edit-and-Continue) of script assemblies.
- Bindings or expressions in markup documents (ADR-0065), and a markup tag naming a Razor component.
- A C++ Razor: Razor components are C# only.
- Rider-specific editor support beyond what the stock tooling gives.
- Virtualised lists and a component library beyond `Document` (dropdowns, tabs and the rest are FenrirUI's out-of-scope list).

## Further Notes

- **Research**: `research/razor-compilation-and-builder.md` holds the experiment's facts: the stand-in that builds, the generated code's calls, build-time measurements, the keying and cost sketch, and the open questions this spec answers (outro indices, `<style>` per instance, entities in folded markup) or leaves (Rider, the SDK's generator path across versions).
- **Ticket shape**: the build stage first, as the prefactor. Then a tracer bullet: a static `.razor` component compiled, mounted and on screen, through every layer, with reload. Then rebuilds in place, child components, and handlers and binding over it. Native scoping runs beside the tracer, and the component stylesheets join it. Paint overrides and documents follow, and UIDemo comes last.
- **Branches**: an integration branch `razor-ui` cut from the base this spec merges into, one ticket session, branch and PR per ticket into `razor-ui` (or the previous ticket's branch when stacked), and a trunk PR from `razor-ui` to its base describing the whole stack.
- **Vocabulary**: see GLOSSARY.md's "FenrirEngine painting and UI" section (Razor component, mount, Razor dispatcher, build stage, scope class), beside element, element id, UI root and stylesheet.

## Tickets

One session, branch and PR per ticket, into `razor-ui` or the previous ticket's branch when stacked. The PR column is kept current as each opens.

Stacks, from the Touches lines:
- 01 → 02, on FenrirUI's native files.
- 03 is cut from `razor-ui` once 01 has merged, and runs beside 02.
- 03 → 04 → 05 → 06 → 07 → 08 stack on `Fenrir.UI.Razor`, with 07 cut once 02 has merged.
- 09 comes last.

| # | Ticket | Blocked by | Branch | PR |
|---|---|---|---|---|
| 01 | [The build stage](issues/01-the-build-stage.md) | none | `razor/01-the-build-stage` | [#364](https://github.com/Wolfleader101/FenrirEngine/pull/364) |
| 02 | [Native scoped stylesheets](issues/02-native-scoped-stylesheets.md) | none | `razor/02-native-scoped-stylesheets` | [#365](https://github.com/Wolfleader101/FenrirEngine/pull/365) |
| 03 | [Tracer: a static Razor component on screen](issues/03-tracer-a-static-razor-component-on-screen.md) | 01 | `razor/03-tracer-a-static-razor-component-on-screen` | |
| 04 | [Rebuilds in place](issues/04-rebuilds-in-place.md) | 03 | `razor/04-rebuilds-in-place` | |
| 05 | [Child components](issues/05-child-components.md) | 04 | `razor/05-child-components` | |
| 06 | [Handlers and two-way binding](issues/06-handlers-and-two-way-binding.md) | 04, 05 | `razor/06-handlers-and-two-way-binding` | |
| 07 | [Scoped component stylesheets](issues/07-scoped-component-stylesheets.md) | 02, 04 | `razor/07-scoped-component-stylesheets` | |
| 08 | [Paint overrides and `<Document>`](issues/08-paint-overrides-and-document.md) | 04 | `razor/08-paint-overrides-and-document` | |
| 09 | [UIDemo: the Razor speedometer and settings page](issues/09-uidemo-the-razor-speedometer-and-settings-page.md) | 06, 07, 08 | `razor/09-uidemo-the-razor-speedometer-and-settings-page` | |
