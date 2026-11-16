---
status: accepted
---

# Razor components compile with the stock SDK generator against an engine stand-in, and build FenrirUI elements in place

ADR-0061 left a Razor-style syntax to a later effort, "a C# source generator over the same element API, as s&box's Razor is a convenience over its Panel API". This is that effort. s&box vendors a modified `Microsoft.AspNetCore.Razor.Language` and replaces Blazor's runtime with a five-file stand-in (`.scratch/fenrir-ui/research/sbox-ui-stack.md`). Research on 2026-11-14 (`.scratch/razor-ui/research/razor-compilation-and-builder.md`) built `.razor` files with the stock .NET 10 SDK against a stand-in of the engine's own, and measured an in-place builder over FenrirUI's natives. Settled in a grilling session on 2026-11-15.

## The stock generator, a stand-in and a runtime

- **The SDK's own Razor source generator compiles `.razor` files** in an ordinary script project (`Microsoft.NET.Sdk`). `Fenrir.UI.Razor.props`, which FenrirUI ships beside its contracts and a script project imports, wires it in (FenrirScripting knows no package, ADR-0009): the generator dll from the running SDK as an `Analyzer`, the `.razor` files as `AdditionalFiles`, and `RazorLangVersion` as a compiler-visible property. The Razor SDK's CSS isolation is not used; scoping is native (ADR-0070). Nothing is forked or vendored.
- **`Microsoft.AspNetCore.Components`** is a stand-in contract of the engine's, shared like every contract (ADR-0004). It holds only the names the generated code and the generator's discovery need: `IComponent`, `ComponentBase` with `BuildRenderTree`, an abstract `RenderTreeBuilder`, `RenderFragment`, `EventCallback`, `ElementReference`, `BindConverter`, `ParameterAttribute`, the `RuntimeHelpers` the generator calls, and the two discovery classes `EventHandlers` and `BindAttributes`, which list FenrirUI's UI event kinds and value controls (ADR-0069). It references the `Fenrir.UI` contract, as `EventHandlers` names `Fenrir.UI.UIEvent` as every handler's argument (*amended 2026-11-16:* ticket 06 found the generator takes the struct itself, so there is no `UIEventArgs` wrapper). The generator finds tag helpers only in assemblies whose names begin `Microsoft.AspNetCore.`, so the name is not a choice.
- **`Fenrir.UI.Razor`** is the runtime contract: the `UIComponent` base class every `.razor` file derives from, the builder, mounting and the dispatcher's runtime (ADR-0068). It builds on `Fenrir.UI` and the stand-in, and nothing native calls it: FenrirUI's scripting extension only names the two assemblies' files among the contracts it shares (its binding's `[registration] shares`).

## Elements are built in place by sequence number

The generated `BuildRenderTree` calls the builder with integer-literal sequence numbers, one counter per file. The builder is s&box's model, not Blazor's: there is no frame array and no diff. It creates, updates and removes FenrirUI elements while the code runs.

- **Keys.** An element is keyed within its parent's region by its sequence number and its occurrence count in this build, or by its sequence number and its `@key` value. A child Razor component, a `RenderFragment` and `ChildContent` each open a region of their own, since their numbers belong to the file that wrote them. `SetKey` arrives after an element's attributes, so the builder resolves an element at its first non-attribute call.
- **Changes.** The builder keeps the last value of every attribute, class token, style declaration and text run. It calls a native only when a value changed: `AddClass` and `RemoveClass` per token, `SetStyle` per declaration, `SetIdString`, `SetText`, `SetValue` or `SetEntryText` for a value, and `SetAttribute` for the rest. A rebuild that changes nothing makes no native call.
- **Removal and order.** An element its region did not visit this build is removed with its outro (ADR-0066), and the builder counts outgoing elements in the indices it passes until they go. New elements are made last and moved with `Reorder` only when they fall outside the longest increasing run of the region's old order.
- **Tags.** A lowercase tag is a native element through `CreateElement`: a built-in control, a registered custom tag, or a plain panel for any other name. Text in a `label` or `button` is its text. Loose text in any other element becomes a generated `label`. An uppercase tag is a Razor component type.
- **Folded markup.** The stock compiler folds static markup, `<style>` blocks included, into `AddMarkupContent("<…>")`, and has no switch to stop it. The builder parses each folded string in C#, once per key, into the same operations, so a folded subtree follows the same rules as the rest of the file.
- **Child Razor components.** `OpenComponent<T>` finds or makes the child at its key. Parameters are set by name through setters compiled once per type from its `[Parameter]` properties, and cached. The engine's own generators cannot see `@code` members, because source generators do not chain.

## Razor and markup documents include each other the cheap way

- **Razor places a document** with the built-in `<Document Src="…" />`, which calls `LoadDocument` into its element. The builder owns the host element and treats what the document made as opaque: it never visits, updates or removes it, except with its host.
- **A document reaches Razor** only through an element that C# mounts a Razor component into (`root.Mount<T>(element)`, ADR-0068), found by id like any other. Markup gains no tag for it, and FenrirUI's C++ learns nothing about Razor.

## Considered

- **Vendoring the Razor compiler, as s&box does.** About 600 files and 3 MB of MIT source to keep up with each SDK. A fork buys unfolded markup, `<style>` as its own call, s&box's `Name:bind` syntax, uppercase tags as classes and typed parameter setters. But editors would still run the stock tooling and disagree with the fork. The compiler's NuGet toolset package is preview-only and not supported for long-term use, so a pinned compiler means a fork.
- **Blazor's renderer: a frame array per Razor component and a diff.** It builds and compares a whole frame array every render, when the builder can compare each value at its call site against what it last set.
- **Folded markup through native `LoadMarkup`.** One parser, but native markup rejects loose text in a panel and has no scope class, so a folded subtree would follow different rules from the elements beside it.
- **Parameters only in code-behind files**, where a Fenrir generator could see them and emit typed setters. It forbids the `@code` block for the most common thing a Razor component declares.
- **A markup tag that names a Razor component.** FenrirUI's parser would learn a tag that does nothing natively.

## Consequences

- The generator follows the installed SDK. A new SDK can change what the generated code calls, and the stand-in grows to match; the scripting tests that compile `.razor` fixtures catch it.
- A typo in a directive attribute, a missing `@using` or a stand-in that lost a discovery class turns `@onclick` or `@bind` into a plain attribute without an error. The builder leaves out, and warns once per Razor component type and sequence number about, an attribute whose name begins with `@` or is a handler directive's name in any case (`onclick`), naming the missing `@using`. *Amended 2026-11-16 (close-out):* first written as once per element for any name beginning with `on`, which would also warn about a game's own `on…` attributes.
- Editors give `.razor` files the stock Razor experience in Visual Studio and VS Code, from the same generator. Rider uses its own implementation and is unverified.
- *Amends ADR-0061:* the Razor-style syntax is this ADR, ADR-0068, ADR-0069 and ADR-0070.
