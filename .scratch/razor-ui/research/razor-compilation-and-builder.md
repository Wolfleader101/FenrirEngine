# Compiling `.razor` with the stock SDK against a stand-in, and building FenrirUI elements in place

Research note, 2026-11-14. Builds on [`.scratch/fenrir-ui/research/sbox-ui-stack.md`](../../fenrir-ui/research/sbox-ui-stack.md)
sections 1.1-1.3 and 3.3 (s&box's vendored Razor, its five-file stand-in and `PanelRenderTreeBuilder`), which is not
repeated here.

Sources, pinned:

- **The Razor compiler the SDK runs.** .NET SDK 10.0.402 ships `Sdks/Microsoft.NET.Sdk.Razor/source-generators/Microsoft.CodeAnalysis.Razor.Compiler.dll`,
  product version `10.4.0-preview.26505.111+a8dd0577c5c13f43a2f57fe33b4ff11b90f6371e` (read from the installed file).
  `a8dd0577` is tag [`v10.0.402`](https://github.com/dotnet/dotnet/tree/v10.0.402) of the dotnet/dotnet VMR, whose
  [source-manifest.json](https://github.com/dotnet/dotnet/blob/v10.0.402/src/source-manifest.json) pins razor at
  [`dotnet/razor@4fb38398`](https://github.com/dotnet/razor/tree/4fb38398b3058d2040cfdb033a1aae3945ddb78a). The public
  VMR tag carries `src/sdk` but not `src/razor`, so compiler links below point at dotnet/razor@4fb38398, and the Razor
  SDK's MSBuild targets at the VMR tag. `RZ` = `https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/`
  is used as shorthand in link text only. Every link is a full URL.
- **Blazor's runtime:** [`dotnet/aspnetcore@v10.0.0`](https://github.com/dotnet/aspnetcore/tree/v10.0.0).
- **Roslyn docs:** [`dotnet/roslyn@02b35215`](https://github.com/dotnet/roslyn/tree/02b3521586eb6dd9e6e4e62b6bec0f2e35b8cc60),
  the roslyn commit the same VMR manifest pins.
- **The runtime:** [`dotnet/runtime@v10.0.0`](https://github.com/dotnet/runtime/tree/v10.0.0).
- **s&box:** [`Facepunch/sbox-public@2643ebd6`](https://github.com/Facepunch/sbox-public/tree/2643ebd62bf7947129fa4f9d8f490d7580be7903), the commit the s&box note used.
- **Microsoft Learn** pages, as cited.
- **The experiment** in `C:\Users\james\AppData\Local\Temp\claude\10bd9b4057b1b449\a55d3c00-aa8e-4a80-8d82-d9f875ff7b7e\scratchpad\razor-exp\`
  (scratch, may be cleared). It holds `Stub/` (the stand-in), `Scripts/` (Razor SDK project), `PlainSdk/` (plain SDK
  with the generator wired by hand), `R2/` and `Stub2/` (stand-in renamed), `ProbeGen/` (a generator that reports what
  it sees), `Runner/` (runs the generated code: a printing builder and an in-place builder sketch over fake natives),
  `T-*/` with `time.sh` (build timings), and `gen/iterN/` (the generated C# captured at each iteration). Machine: i7-13700KF
  (24 logical CPUs), 32 GB, Windows 11 Pro 10.0.26200, SDK 10.0.402 pinned by `global.json`.

## Findings

1. **Yes, the stock generator compiles `.razor` against your own stand-in, with no ASP.NET Core reference.** A plain
   `net10.0` class library on `Microsoft.NET.Sdk.Razor`, with no `<FrameworkReference>`, built `.razor` files against
   a ~190-line stand-in. It also built on plain `Microsoft.NET.Sdk`, with the SDK's generator dll added as an `Analyzer`,
   the `.razor` files as `AdditionalFiles`, and `RazorLangVersion` passed as a `CompilerVisibleProperty`. The generated C#
   was identical either way. The output's runtimeconfig names only `Microsoft.NETCore.App`.
2. **The stand-in has to be an assembly named `Microsoft.AspNetCore.Components`, or at least `Microsoft.AspNetCore.*`.**
   Tag-helper discovery skips any assembly whose name does not start with `Microsoft.AspNetCore.`, and any assembly
   that references none that does. Renamed to `Fenrir.UI.Razor`, the same stand-in still compiled, but silently: no
   components, no `@onclick`, no `@ref`. `AddComponentParameter` is used only when a reference's file name ends in
   `Microsoft.AspNetCore.Components.dll`. Otherwise the generator emits `AddAttribute` for component parameters. No
   version is checked: the stand-in was 1.0.0.0, unsigned.
3. **What each directive needs from the stand-in, all found by full type name:**
   - Components: `IComponent`.
   - `@key` and `@attributes`: `RenderTreeBuilder` existing.
   - `@ref`: `ElementReference`.
   - `@onclick`: `EventHandlerAttribute`, placed on a public class named exactly `EventHandlers`. Its namespace must be
     `@using`-imported; `Microsoft.AspNetCore.Components` is imported by default.
   - `@bind`: `BindConverter` existing, plus `[BindElement]`/`[BindInputElement]` on a public class named `BindAttributes`.

   Anything unrecognised is emitted silently as a literal attribute or as markup text, with no diagnostic
   ([dotnet/razor#13210](https://github.com/dotnet/razor/issues/13210)).
4. **The generated `BuildRenderTree` called 18 builder members**, plus helper types: `ComponentBase`, the `RenderFragment`
   delegates, `EventCallback`/`EventCallback<T>` with `EventCallback.Factory.Create`/`CreateBinder`, `BindConverter.FormatValue`,
   and `RuntimeHelpers.TypeCheck`/`CreateInferredEventCallback`. Sequence numbers are integer literals: one counter
   per file, starting at 0. `SetKey` comes *after* the element's attributes. `ChildContent` is passed with `AddAttribute`,
   not `AddComponentParameter`.
5. **Static markup always collapses into `AddMarkupContent("<html string>")`, and the stock compiler has no switch to
   stop it.** The pass is registered unconditionally and skipped only at design time. `<style>` with no `@` inside becomes
   part of such a string. To get elements, you either parse the string at runtime (s&box does this once per block), or
   vendor the compiler. FenrirUI's C++ already has `UIRoot::LoadMarkup(parent, text, name)` and `AddStylesheet(text, name)`,
   but neither is exposed to C#.
6. **Generators do not chain.** Roslyn's docs say each generator sees the same input compilation and none of the others'
   output. The experiment confirmed it: a probe generator saw only the `.razor.cs` code-behind half of a component. It
   did not see the Razor half's base type, its `@code` members, or the `.razor`-only components. So `@attribute [System]`
   in a `.razor` file is invisible to SystemGenerator, while the same attribute on a code-behind partial is visible.
7. **Vendoring costs and buys:**
   - Cost: the compiler is MIT, about 622 C# files / 3.1 MB in `Microsoft.CodeAnalysis.Razor.Compiler/src` plus 104 files /
     0.66 MB of shared utilities (s&box vendors 435 files / 2.1 MB).
   - Buys: what s&box's edits give (no markup collapse for chosen tags, `<style>` → `AddStyleDefinitions`, `Name:bind`,
     uppercase tag = class, keys passed to `OpenElement`, source locations).
   - The no-vendoring pinning route is thin. `Microsoft.Net.Compilers.Razor.Toolset` on nuget.org has three previews, the
     newest `9.0.0-preview.25264.2`, and it says it is for hotfixes and "explicitly not supported" long-term.
     `Microsoft.CodeAnalysis.Razor.Compiler` has one preview. `Microsoft.AspNetCore.Razor.Language` stopped at 6.0.36.
8. **Build time cost is small.** With 4 `.razor` files, a warm clean build took a median of 1.34-1.86 s, against 1.21 s
   for the same project without Razor. The Razor generator itself reported 0.07 s for 4 files and 0.52 s for 54. Editing
   one `.razor` in a 54-file project took 1.33 s (median) against 0.81 s for editing a `.cs` file in the baseline.
9. **Hot reload:** a rebuilt dll is ordinary C#, so Fenrir's reload-the-whole-assembly path needs nothing Razor-specific.
   The state lost is component instances, delegates and builder caches that reference the old ALC. Metadata-update hot
   reload (`ApplyUpdate`) needs a module with Edit-and-Continue enabled. The v10.0.0 runtime's EnC-capability check does
   not mention collectibility. Whether it works in a collectible ALC was not tested.
10. **An in-place builder costs zero native calls for a no-change rebuild.** The sketch (fake natives) does that. A text
    change costs 1 `SetText`, a class change 1 `AddClass`, a keyed insert 3 calls, a keyed remove 1 `Remove`, and a
    keyed reorder of 2 items 1 `Reorder`. The managed cost of an unoptimised no-change rebuild of a 15-element tree was
    about 12 µs and 11 KB allocated. Blazor diffs frame arrays by sequence number, and s&box mutates panels keyed by
    sequence + loop counter. This builder does the s&box thing over `UIRoot` handles.

## 1. The stock Razor generator against a stand-in

### 1.1 What the SDK does, and the two ways to switch it on

- **`Microsoft.NET.Sdk.Razor`:** for a `net10.0` target, the Razor SDK sets `UseRazorSourceGenerator=true` and
  `RazorLangVersion=10.0`
  ([Sdk.Razor.CurrentVersion.targets:56-57](https://github.com/dotnet/dotnet/blob/v10.0.402/src/sdk/src/RazorSdk/Targets/Sdk.Razor.CurrentVersion.targets#L56-L57)).
  It turns `.razor` Content into `RazorComponent` items (lines [580-581](https://github.com/dotnet/dotnet/blob/v10.0.402/src/sdk/src/RazorSdk/Targets/Sdk.Razor.CurrentVersion.targets#L580-L581)).
  It then adds every dll in `source-generators/` as an `Analyzer`, passes the components as `AdditionalFiles`, and makes
  `RazorLangVersion`, `RootNamespace` and `MSBuildProjectDirectory` compiler-visible
  ([Microsoft.NET.Sdk.Razor.SourceGenerators.targets:25-71](https://github.com/dotnet/dotnet/blob/v10.0.402/src/sdk/src/RazorSdk/Targets/Microsoft.NET.Sdk.Razor.SourceGenerators.targets#L25-L71)).
  - No `FrameworkReference` to `Microsoft.AspNetCore.App` is added; MVC support is a separate `AddRazorSupportForMvc` opt-in.
  - The SDK also turns on static web assets: `bin/` gained `Exp.Scripts.staticwebassets.endpoints.json`, and `obj/`
    gained `EmbeddedAttribute.cs` and `ValidatableTypeAttribute.cs` (experiment: `ls` of `T-razor` vs `T-plain` outputs).
- **Plain `Microsoft.NET.Sdk`:** the same generator, wired by hand, produced byte-identical C# (bar `#line` paths) and
  no static-web-assets files (experiment: `PlainSdk/PlainSdk.csproj`, `diff` of the generated `Counter_razor.g.cs`).
  The generator needs only this project change:

  ```xml
  <PropertyGroup>
    <RazorLangVersion>10.0</RazorLangVersion>
    <RazorGeneratorDir>$(NetCoreRoot)sdk\$(NETCoreSdkVersion)\Sdks\Microsoft.NET.Sdk.Razor\source-generators\</RazorGeneratorDir>
  </PropertyGroup>
  <ItemGroup>
    <Analyzer Include="$(RazorGeneratorDir)Microsoft.CodeAnalysis.Razor.Compiler.dll" />
    <Analyzer Include="$(RazorGeneratorDir)Microsoft.AspNetCore.Razor.Utilities.Shared.dll" />
    <AdditionalFiles Include="**\*.razor" />
    <CompilerVisibleProperty Include="RazorLangVersion" />
    <CompilerVisibleProperty Include="RootNamespace" />
    <CompilerVisibleProperty Include="MSBuildProjectDirectory" />
  </ItemGroup>
  ```

  The generator supports this explicitly. When no `TargetPath` metadata is given, it derives the relative path from
  `MSBuildProjectDirectory`: "an easy way for a project that isn't using the Razor SDK"
  ([SourceGenerators/RazorSourceGenerator.RazorProviders.cs:82-90](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/SourceGenerators/RazorSourceGenerator.RazorProviders.cs#L82-L90)).
  Without `RazorLangVersion`, it reports a diagnostic and uses Latest (same file, lines
  [33-42](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/SourceGenerators/RazorSourceGenerator.RazorProviders.cs#L33-L42)).
  The path ties the project to whichever SDK builds it; see 2.3.
- **With no stand-in at all,** the generator still ran. The project then failed with `CS0234` (namespace
  `Microsoft.AspNetCore` missing), `CS0246` (`Parameter`, `RenderFragment`), and `RZ10012` for the child component's tag
  (experiment: `gen/iter0-nostub`). `@onclick`, `@bind` and `@key` came out as literal HTML attributes inside
  `AddMarkupContent` strings.

### 1.2 How it discovers components and directive attributes

The generator builds a "declaration" compilation (the `.razor` classes with no bodies) and discovers tag helpers in it
([RazorSourceGenerator.cs:109-133](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/SourceGenerators/RazorSourceGenerator.cs#L109-L133)).
It then discovers them in every metadata reference
([:222-231](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/SourceGenerators/RazorSourceGenerator.cs#L222-L231)).
The producers are registered for Razor 3.0+: Bind, Component, EventHandler, Ref, Key, Splat; and for 8.0+: RenderMode,
FormName
([CSharp/CompilerFeatures.cs:20-34](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/CSharp/CompilerFeatures.cs#L20-L34)).

- **The assembly-name gate.** Before any producer looks at an assembly, `MightContainTagHelpers` requires that "the
  assembly must start with "Microsoft.AspNetCore." or reference an assembly that starts with "Microsoft.AspNetCore.""
  ([Language/SymbolCache.AssemblySymbolData.cs:28-36](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/SymbolCache.AssemblySymbolData.cs#L28-L36),
  used at [Language/TagHelperDiscoverer.cs:56-61](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelperDiscoverer.cs#L56-L61)).
  - Experiment (`R2/` + `Stub2/`): the identical stand-in compiled as `Fenrir.UI.Razor.dll` built with only warnings.
    `<Panel>`/`<Slider>`/`<Healthbar>` gave `RZ10012`, `@ref` fields went unassigned (`CS0169`), and no
    `EventCallback.Factory.Create` was emitted.
  - It follows that a contract assembly (say `Fenrir.UI`) holding components of its own would have them found only if it
    references a `Microsoft.AspNetCore.*` assembly. The scripts assembly itself always references the stand-in.
- **The component gate:** a type is a component if it is public, non-abstract, and implements an interface whose full
  name is `Microsoft.AspNetCore.Components.IComponent`
  ([CSharp/ComponentDetectionConventions.cs:28-45](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/CSharp/ComponentDetectionConventions.cs#L28-L45),
  [ComponentTagHelperProducer.cs:32-33](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/ComponentTagHelperProducer.cs#L32-L33)).
  The tag is the type's short name. A `.razor` file with no `@inherits` derives from `global::Microsoft.AspNetCore.Components.ComponentBase`
  and overrides `BuildRenderTree`
  ([ComponentDocumentClassifierPass.cs:100](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentDocumentClassifierPass.cs#L100),
  [:128](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentDocumentClassifierPass.cs#L128)).
  `@inherits ScriptComponent`, with a `ScriptComponent : ComponentBase` of the scripts project's own, worked
  (experiment: `Extra.razor`).
- **Event handlers:** the producer exists only if `Microsoft.AspNetCore.Components.EventHandlerAttribute` exists
  ([EventHandlerTagHelperProducer.Factory.cs:20-24](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/EventHandlerTagHelperProducer.Factory.cs#L20-L24)).
  It reads `[EventHandler(name, argsType, stopPropagation, preventDefault)]` off any public type named exactly `EventHandlers`
  ([EventHandlerTagHelperProducer.cs:24-50](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/EventHandlerTagHelperProducer.cs#L24-L50)).
  - The args type becomes `T` in `EventCallback.Factory.Create<T>(this, handler)`.
  - The two booleans enable `@onclick:stopPropagation` / `:preventDefault`, emitted as `AddEventStopPropagationAttribute`
    (experiment: `Extra.razor`).
  - Namespace scoping: with `EventHandlers` in `Microsoft.AspNetCore.Components.Web` and no `@using`, `@onclick` became
    literal markup text with no diagnostic. Adding `_Imports.razor` with `@using Microsoft.AspNetCore.Components.Web`
    fixed it (experiment: `gen/iter8-webns`, `gen/iter9-imports`; same behaviour reported in
    [dotnet/razor#13210](https://github.com/dotnet/razor/issues/13210), closed as a duplicate of #7650). Putting
    `EventHandlers` in `Microsoft.AspNetCore.Components` needs no import, because the generated file always has
    `using global::Microsoft.AspNetCore.Components;`.
- **`@bind`:** the producer exists only if `BindConverter` exists. The general `@bind-xxx` fallback is added when the
  stand-in assembly is scanned. Per-element rules come from `[BindElement]`/`[BindInputElement]` on a public type named
  `BindAttributes`, and only when both attribute types exist
  ([BindTagHelperProducer.Factory.cs:20-30](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/BindTagHelperProducer.Factory.cs#L20-L30),
  [BindTagHelperProducer.cs:99-117](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/BindTagHelperProducer.cs#L99-L117)).
- **`@key` and `@attributes`:** added when the assembly that declares `Microsoft.AspNetCore.Components.Rendering.RenderTreeBuilder`
  is scanned
  ([KeyTagHelperProducer.cs:23-31](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/KeyTagHelperProducer.cs#L23-L31);
  Splat the same).
- **`@ref`:** gated on `ElementReference`
  ([RefTagHelperProducer.Factory.cs:20](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/TagHelpers/Producers/RefTagHelperProducer.Factory.cs#L20)).
- **`AddComponentParameter` vs `AddAttribute`:** the generator uses `AddComponentParameter` only if a reference whose
  path ends in `Microsoft.AspNetCore.Components.dll` declares a public `RenderTreeBuilder.AddComponentParameter`
  ([RazorSourceGenerator.RazorProviders.cs:44-52](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/SourceGenerators/RazorSourceGenerator.RazorProviders.cs#L44-L52),
  [CSharp/CompilationExtensions.cs:11-18](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/CSharp/CompilationExtensions.cs#L11-L18)).
  Experiment, iteration 1: the stand-in lacked it, and parameters came out as `AddAttribute(seq, nameof(Panel.Title), ...)`.
- **Names the compiler hard-codes** are listed in
  [Language/Components/ComponentsApi.cs](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentsApi.cs).
  - `AssemblyName = "Microsoft.AspNetCore.Components"` at line 12.
  - Also `RouteAttribute`, `LayoutAttribute`, `InjectAttribute`, `IComponentRenderMode`, `CascadingTypeParameterAttribute`,
    `RuntimeHelpers.CreateInferredBindSetter`/`InvokeSynchronousDelegate`/`InvokeAsynchronousDelegate`, `OpenRegion`
    and `AddNamedEvent`. None of these was needed by the files tested; see Open questions.

### 1.3 Iterations: every error and what fixed it

| # | Setup | Result | Fix |
| --- | --- | --- | --- |
| 0 | No stand-in (`-p:NoStub=true`) | 12× `CS0234` `Microsoft.AspNetCore` missing; `CS0246` `Parameter`/`ParameterAttribute`/`RenderFragment`; `RZ10012` for `<Panel>` | add the stand-in |
| 1 | Stand-in with `IComponent`, `ComponentBase`, `ParameterAttribute`, `RenderFragment`(`<T>`), builder with `OpenElement`/`CloseElement`/`AddMarkupContent`/`AddContent`×3/`AddAttribute`×2 | `CS0234` `CompilerServices` missing; `CS1061` no `OpenComponent`, `CloseComponent`, `SetKey`. `@onclick`/`@bind` still literal markup; parameters via `AddAttribute` | add `CompilerServices.RuntimeHelpers.TypeCheck`, component and key members |
| 2 | Plus `ElementReference`, `EventHandlerAttribute` + `EventHandlers`, `Bind*Attribute` + `BindAttributes`, `BindConverter`, `EventCallback`(`<T>`), `EventCallbackFactory`, `CreateBinder` extensions, `AddComponentParameter`, `SetUpdatesAttributeName`, reference captures, stop/prevent attributes | **builds clean**; `@onclick` → `EventCallback.Factory.Create<MouseEventArgs>`; `@bind` → `FormatValue` + `CreateBinder` + `SetUpdatesAttributeName`; parameters → `AddComponentParameter` | none |
| 3 | Add `Extra.razor` (element `@ref`, `@onclick` with arg + `:stopPropagation`, `@bind-Value` on a child component, `Value:bind=`, unknown uppercase tag, `@if/else`, templated `@<p>`) and `Slider.razor` | `CS0117` no `RuntimeHelpers.CreateInferredEventCallback`; `CS1061` no `EventCallback<float>.InvokeAsync` (used by the user's own code); `RZ10012` for `<Healthbar>` (intended) | add both |
| 4 | | builds; `RZ10012` warning only | |
| 5-10 | `EventHandlers` moved to `.Web` namespace, with and without `@using` | without: `@onclick` silently literal; with: handlers bound | see 1.2 |
| 11 | Stand-in assembly renamed `Fenrir.UI.Razor` | builds, components and directives silently not recognised | name it `Microsoft.AspNetCore.Components` |
| 12 | `@typeparam` component with `RenderFragment<TItem>` + `Context=`, and `@attributes` | `CS1061` no `AddMultipleAttributes` | add it |
| 13 | | builds | |

### 1.4 The minimal working stand-in

This is everything the tested files needed; nothing else was declared. The builder is abstract, so the engine
supplies the implementation (s&box's is abstract too). The event-args types are the engine's to choose: the
compiler only copies the type named in `[EventHandler]`.

```csharp
// Assembly name: Microsoft.AspNetCore.Components (see 1.2 for why the name matters). No version or signing required.
namespace Microsoft.AspNetCore.Components
{
    using Microsoft.AspNetCore.Components.Rendering;

    public interface IComponent { }                                   // component discovery, by full name
    public abstract class ComponentBase : IComponent                  // default base of a .razor class
    {
        protected virtual void BuildRenderTree(RenderTreeBuilder __builder) { }
    }
    [AttributeUsage(AttributeTargets.Property)] public sealed class ParameterAttribute : Attribute { }
    public delegate void RenderFragment(RenderTreeBuilder builder);
    public delegate RenderFragment RenderFragment<TValue>(TValue value);
    public readonly struct ElementReference                           // gates @ref
    { public ElementReference(string id) { Id = id; } public string Id { get; } }

    [AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]    // gates @on*
    public sealed class EventHandlerAttribute : Attribute
    {
        public EventHandlerAttribute(string attributeName, Type eventArgsType) : this(attributeName, eventArgsType, false, false) { }
        public EventHandlerAttribute(string attributeName, Type eventArgsType, bool enableStopPropagation, bool enablePreventDefault)
        { AttributeName = attributeName; EventArgsType = eventArgsType; EnableStopPropagation = enableStopPropagation; EnablePreventDefault = enablePreventDefault; }
        public string AttributeName { get; } public Type EventArgsType { get; }
        public bool EnableStopPropagation { get; } public bool EnablePreventDefault { get; }
    }
    public class MouseEventArgs : EventArgs { }
    public class ChangeEventArgs : EventArgs { public object? Value { get; set; } }
    [EventHandler("onclick", typeof(MouseEventArgs), true, true)]   // the class name "EventHandlers" is what is matched
    [EventHandler("onchange", typeof(ChangeEventArgs), true, true)]
    [EventHandler("oninput", typeof(ChangeEventArgs), true, true)]
    public static class EventHandlers { }

    [AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
    public sealed class BindElementAttribute : Attribute
    {
        public BindElementAttribute(string element, string? suffix, string valueAttribute, string changeAttribute)
        { Element = element; Suffix = suffix; ValueAttribute = valueAttribute; ChangeAttribute = changeAttribute; }
        public string Element { get; } public string? Suffix { get; } public string ValueAttribute { get; } public string ChangeAttribute { get; }
    }
    [AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
    public sealed class BindInputElementAttribute : Attribute
    {
        public BindInputElementAttribute(string? type, string? suffix, string? valueAttribute, string? changeAttribute, bool isInvariantCulture, string? format)
        { Type = type; Suffix = suffix; ValueAttribute = valueAttribute; ChangeAttribute = changeAttribute; IsInvariantCulture = isInvariantCulture; Format = format; }
        public string? Type { get; } public string? Suffix { get; } public string? ValueAttribute { get; } public string? ChangeAttribute { get; }
        public bool IsInvariantCulture { get; } public string? Format { get; }
    }
    [BindInputElement(null, null, "value", "onchange", false, null)]  // the class name "BindAttributes" is what is matched
    [BindInputElement("range", null, "value", "onchange", true, null)]
    [BindElement("select", null, "value", "onchange")]
    public static class BindAttributes { }
    public static class BindConverter                                 // gates @bind
    {
        public static string? FormatValue(string? value, System.Globalization.CultureInfo? culture = null) => value;
        public static string FormatValue(int value, System.Globalization.CultureInfo? culture = null) => value.ToString(culture);
        public static string FormatValue(float value, System.Globalization.CultureInfo? culture = null) => value.ToString(culture);
        public static object? FormatValue<T>(T value, System.Globalization.CultureInfo? culture = null) => value;
    }

    public readonly struct EventCallback
    {
        public static readonly EventCallbackFactory Factory = new();
        internal EventCallback(object? receiver, Delegate? @delegate) { Receiver = receiver; Delegate = @delegate; }
        public object? Receiver { get; } public Delegate? Delegate { get; }
    }
    public readonly struct EventCallback<TValue>
    {
        internal EventCallback(object? receiver, Delegate? @delegate) { Receiver = receiver; Delegate = @delegate; }
        public object? Receiver { get; } public Delegate? Delegate { get; }
        public Task InvokeAsync(TValue arg) { /* call Delegate as Action<TValue>, Func<TValue,Task> or Action */ return Task.CompletedTask; }
    }
    public sealed class EventCallbackFactory
    {
        public EventCallback Create(object receiver, Action callback) => new(receiver, callback);
        public EventCallback Create(object receiver, Action<object> callback) => new(receiver, callback);
        public EventCallback Create(object receiver, Func<Task> callback) => new(receiver, callback);
        public EventCallback Create(object receiver, Func<object, Task> callback) => new(receiver, callback);
        public EventCallback Create(object receiver, EventCallback callback) => callback;
        public EventCallback<TValue> Create<TValue>(object receiver, Action callback) => new(receiver, callback);
        public EventCallback<TValue> Create<TValue>(object receiver, Action<TValue> callback) => new(receiver, callback);
        public EventCallback<TValue> Create<TValue>(object receiver, Func<Task> callback) => new(receiver, callback);
        public EventCallback<TValue> Create<TValue>(object receiver, Func<TValue, Task> callback) => new(receiver, callback);
        public EventCallback<TValue> Create<TValue>(object receiver, EventCallback<TValue> callback) => callback;
    }
    public static class EventCallbackFactoryBinderExtensions           // @bind's setter: Factory.CreateBinder(this, __value => X = __value, X)
    {
        public static EventCallback<ChangeEventArgs> CreateBinder(this EventCallbackFactory factory, object receiver,
            Action<string?> setter, string? existingValue, System.Globalization.CultureInfo? culture = null) => default;
        public static EventCallback<ChangeEventArgs> CreateBinder<T>(this EventCallbackFactory factory, object receiver,
            Action<T> setter, T existingValue, System.Globalization.CultureInfo? culture = null) => default;
    }
}
namespace Microsoft.AspNetCore.Components.CompilerServices
{
    public static class RuntimeHelpers
    {
        public static T TypeCheck<T>(T value) => value;                // wraps every typed component parameter
        public static EventCallback<T> CreateInferredEventCallback<T>(object receiver, Action<T> callback, T value) => default;      // @bind-Value on a component
        public static EventCallback<T> CreateInferredEventCallback<T>(object receiver, Func<T, Task> callback, T value) => default;
    }
}
namespace Microsoft.AspNetCore.Components.Rendering
{
    public abstract class RenderTreeBuilder
    {
        public abstract void OpenElement(int sequence, string elementName);
        public abstract void CloseElement();
        public abstract void OpenComponent<TComponent>(int sequence) where TComponent : IComponent;
        public abstract void CloseComponent();
        public abstract void AddMarkupContent(int sequence, string? markupContent);
        public abstract void AddContent(int sequence, string? textContent);
        public abstract void AddContent(int sequence, RenderFragment? fragment);
        public abstract void AddContent(int sequence, object? textContent);
        public abstract void AddAttribute(int sequence, string name);                 // CSS-isolation scope attribute
        public abstract void AddAttribute(int sequence, string name, bool value);     // not hit by the test files
        public abstract void AddAttribute(int sequence, string name, string? value);
        public abstract void AddAttribute(int sequence, string name, MulticastDelegate? value); // ChildContent
        public abstract void AddAttribute(int sequence, string name, EventCallback value);      // not hit by the test files
        public abstract void AddAttribute<TArgument>(int sequence, string name, EventCallback<TArgument> value);
        public abstract void AddAttribute(int sequence, string name, object? value);
        public abstract void AddComponentParameter(int sequence, string name, object? value);
        public abstract void AddMultipleAttributes(int sequence, IEnumerable<KeyValuePair<string, object>>? attributes);
        public abstract void SetKey(object? value);
        public abstract void SetUpdatesAttributeName(string updatesAttributeName);
        public abstract void AddElementReferenceCapture(int sequence, Action<ElementReference> elementReferenceCaptureAction);
        public abstract void AddComponentReferenceCapture(int sequence, Action<object> componentReferenceCaptureAction);
        public abstract void AddEventPreventDefaultAttribute(int sequence, string eventName, bool value);
        public abstract void AddEventStopPropagationAttribute(int sequence, string eventName, bool value);
    }
}
```

The real Blazor builder has the same signatures plus a few extras: `AddContent(int, MarkupString)`,
`AddContent<T>(int, RenderFragment<T>, T)`, `AddAttribute(int, RenderTreeFrame)`, `OpenComponent(int, Type)`,
`AddComponentRenderMode`, `AddNamedEvent`, `OpenRegion`/`CloseRegion`, `Clear` and `GetFrames`
([Rendering/RenderTreeBuilder.cs](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/Rendering/RenderTreeBuilder.cs)).
Overload resolution is plain C#. That leaves room for allocation-free overloads the generator never asks for by name.
For example, a generic `AddContent<T>(int, T)` would win over `object` for value types; this was not tested.

### 1.5 What the generated code looks like

Source (`Counter.razor`, abridged):

```razor
<div class="counter @(Active ? "active" : "")" style="width: @(Width)px" id="counter">
    <h1>Count: @Count</h1>
    <p>Static paragraph with <b>bold</b> text.</p>
    @if (Count > 3) { <span class="warn">High!</span> }
    <ul>
    @foreach (var item in Items) { <li @key="item.Id" class="item">@item.Name (@item.Id)</li> }
    </ul>
    <button @onclick="() => Count++">Add</button>
    <input @bind="Name" />
    <input type="range" @bind="Volume" @bind:event="oninput" />
    <Panel Title="Child" @ref="childRef" Count="@Count"><span>Inside child: @Name</span></Panel>
</div>
<style>
    .counter { color: red; }
</style>
```

Generated (experiment: `gen/iter2/Counter_razor.g.cs`, `#line`/`#nullable` pragmas removed):

```csharp
public partial class Counter : global::Microsoft.AspNetCore.Components.ComponentBase
{
    protected override void BuildRenderTree(global::Microsoft.AspNetCore.Components.Rendering.RenderTreeBuilder __builder)
    {
        __builder.OpenElement(0, "div");
        __builder.AddAttribute(1, "class", "counter" + " " + (Active ? "active" : ""));
        __builder.AddAttribute(2, "style", "width:" + " " + (Width) + "px");
        __builder.AddAttribute(3, "id", "counter");
        __builder.OpenElement(4, "h1");
        __builder.AddContent(5, "Count: ");
        __builder.AddContent(6, Count);                         // AddContent(int, object): boxes
        __builder.CloseElement();
        __builder.AddMarkupContent(7, "\n    ");                // inter-element whitespace
        __builder.AddMarkupContent(8, "<p>Static paragraph with <b>bold</b> text.</p>");
        if (Count > 3)
        {
            __builder.AddMarkupContent(9, "<span class=\"warn\">High!</span>");
        }
        __builder.OpenElement(10, "ul");
        foreach (var item in Items)
        {
            __builder.OpenElement(11, "li");
            __builder.AddAttribute(12, "class", "item");
            __builder.SetKey(item.Id);                          // after the attributes
            __builder.AddContent(13, item.Name);
            __builder.AddContent(14, " (");
            __builder.AddContent(15, item.Id);
            __builder.AddContent(16, ")");
            __builder.CloseElement();
        }
        __builder.CloseElement();
        __builder.AddMarkupContent(17, "\n    ");
        __builder.OpenElement(18, "button");
        __builder.AddAttribute(19, "onclick", global::Microsoft.AspNetCore.Components.EventCallback.Factory
            .Create<global::Microsoft.AspNetCore.Components.MouseEventArgs>(this, () => Count++));
        __builder.AddContent(20, "Add");
        __builder.CloseElement();
        // ...
        __builder.OpenElement(26, "input");
        __builder.AddAttribute(27, "value", global::Microsoft.AspNetCore.Components.BindConverter.FormatValue(Name));
        __builder.AddAttribute(28, "onchange", global::Microsoft.AspNetCore.Components.EventCallback.Factory.CreateBinder(this, __value => Name = __value, Name));
        __builder.SetUpdatesAttributeName("value");
        __builder.CloseElement();
        // ... (the range input: FormatValue(Volume, culture: InvariantCulture), "oninput", CreateBinder(..., culture: ...))
        __builder.OpenComponent<global::Exp.Scripts.Panel>(35);
        __builder.AddComponentParameter(36, nameof(global::Exp.Scripts.Panel.Title), "Child");
        __builder.AddComponentParameter(37, nameof(global::Exp.Scripts.Panel.Count),
            global::Microsoft.AspNetCore.Components.CompilerServices.RuntimeHelpers.TypeCheck<global::System.Int32>(Count));
        __builder.AddAttribute(38, "ChildContent", (global::Microsoft.AspNetCore.Components.RenderFragment)((__builder2) => {
            __builder2.OpenElement(39, "span");                 // the parent file's sequence numbers, run inside the child
            __builder2.AddContent(40, "Inside child: ");
            __builder2.AddContent(41, Name);
            __builder2.CloseElement();
        }));
        __builder.AddComponentReferenceCapture(42, (__value) => { childRef = (global::Exp.Scripts.Panel)__value; });
        __builder.CloseComponent();
        __builder.CloseElement();
        __builder.AddMarkupContent(43, "\n\n");
        __builder.AddMarkupContent(44, "<style>\n    .counter { color: red; }\n</style>");
    }
    // the @code block follows verbatim
}
```

Other shapes seen:

- **Element `@ref`:** `AddElementReferenceCapture(2, __value => { box = __value; })`.
- **`@bind-Value` on a component:** `AddComponentParameter(11, nameof(Slider.ValueChanged), TypeCheck<EventCallback<float>>(EventCallback.Factory.Create<float>(this, RuntimeHelpers.CreateInferredEventCallback(this, __value => Volume = __value, Volume))))`.
- **s&box's `Value:bind=@Volume`:** under the stock compiler it is just `AddAttribute(14, "Value:bind", Volume)`, the
  `object` overload. The getter's value arrives, but no setter.
- **A `@typeparam` component:** emitted through a generated `TypeInference.CreateGrid_0<TItem>(__builder, 0, 1, Names, 2, ...)`
  helper that receives its sequence numbers as arguments, still literals at the call site.
- **`@attributes`:** `AddMultipleAttributes(2, TypeCheck<IEnumerable<KeyValuePair<string, object>>>(Splat))`.

### 1.6 Markup blocks, whitespace and `<style>`

- **Markup blocks.** `ComponentMarkupBlockPass` "Rewrites contiguous subtrees of HTML into a special node type to
  reduce the size of the Render tree". An element is folded unless it contains C#, has diagnostics, is an `<option>`
  under a `<select>`, or (for Razor ≤ 7) is a `<script>`
  ([ComponentMarkupBlockPass.cs:13-18, 154-197, 250-261](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentMarkupBlockPass.cs#L13-L18)).
  - It is skipped only at design time (lines [43-47](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentMarkupBlockPass.cs#L43-L47)).
  - It is registered unconditionally, with the other component passes
    ([RazorProjectEngine.cs:427-448](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/RazorProjectEngine.cs#L427-L448)).
    There is no MSBuild property or directive to turn it off in the stock compiler.
  - The only lever from source is to make an element "non-HTML": put any C# in it (an attribute value `@("x")`, a
    `@key`, an event handler), and that element and its ancestors are not folded.
  - s&box's fork adds its own exclusions: panel-class tags (uppercase), `<root>` and `<style>`
    ([sbox ComponentMarkupBlockPass.cs:159-167](https://github.com/Facepunch/sbox-public/blob/2643ebd62bf7947129fa4f9d8f490d7580be7903/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/Components/ComponentMarkupBlockPass.cs#L159-L167)).
    It still parses the remaining blocks at runtime, once per block, with `Sandbox.Html.Node.Parse`, and on later builds
    only re-indexes the panels it made
    ([RenderTree.Markup.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd62bf7947129fa4f9d8f490d7580be7903/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Markup.cs)).
- **Whitespace.** Whitespace-only runs between elements stay as `AddMarkupContent(seq, "\n    ")`; leading/trailing
  whitespace in elements is trimmed unless `@preservewhitespace true` is set
  ([ComponentWhitespacePass.cs:36-60](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentWhitespacePass.cs#L36-L60)).
  A builder ignores whitespace-only strings, as s&box does.
- **`<style>`.** A `<style>` block with no `@` is folded into a markup string (experiment: sequence 44 above). One that
  contains `@expr` becomes `OpenElement(n, "style")` plus `AddContent`. CSS at-rules (`@media`, `@keyframes`, `@import`)
  in a `.razor` file must be written `@@`, which is Razor's escape for a literal `@`.
- **FenrirUI's fit.** FenrirUI's markup loader already understands `<style>` inside markup: it parses the sheet and adds
  it to the root
  ([packages/FenrirUI/include/FenrirUI/UIRoot.hpp:359-388](../../../packages/FenrirUI/include/FenrirUI/UIRoot.hpp)).
  `UIRoot::LoadMarkup(parent, text, name)` appends the parsed elements after the parent's children and returns them
  (`MarkupLoad::elements`, [Markup.hpp:25-38](../../../packages/FenrirUI/include/FenrirUI/Markup.hpp)). It is not in the
  C# bindings (`UINative.cs` has only the path-based `LoadDocument` and `LoadStylesheet`).
  - FenrirUI's markup has stricter rules than HTML: registered tags only (`panel`, `label`, `button`, `checkbox`, `slider`,
    `progress`, `image`, `scroll`, `textentry`, from `src/UI.cpp:18-26`), and text only in elements that take it
    (`label`, `button`).
  - So Razor markup for FenrirUI would be written in FenrirUI's tags. Blazor's HTML tags (`div`, `p`, `b`) would be
    errors in a folded block, but would become plain panels through `CreateElement` ("a plain panel for any other").
- **Razor CSS isolation.** The Razor SDK's CSS isolation also works in this non-web project. A sibling `Panel.razor.css`
  made the generator add `AddAttribute(n, "b-eapc525hs9")` (the name-only overload) to every element of `Panel.razor`.
  It also wrote `.panel[b-eapc525hs9] { color: blue; }` into `obj/Debug/net10.0/scopedcss/` (experiment). FenrirUI's
  compound selectors are tag, id, classes and pseudo-classes, with no attribute selectors
  ([Stylesheet.hpp:48](../../../packages/FenrirUI/include/FenrirUI/Stylesheet.hpp)), so the output would need mapping
  (for example, to a class) before FenrirUI could use it.

### 1.7 Generators do not see Razor's output

Roslyn's design: generators "Run _un-ordered_, each generator will see the same input compilation, with no access to
files created by other source generators"
([docs/features/source-generators.md:21](https://github.com/dotnet/roslyn/blob/02b3521586eb6dd9e6e4e62b6bec0f2e35b8cc60/docs/features/source-generators.md?plain=1#L21);
the same in [source-generators.cookbook.md:52](https://github.com/dotnet/roslyn/blob/02b3521586eb6dd9e6e4e62b6bec0f2e35b8cc60/docs/features/source-generators.cookbook.md?plain=1#L52)).

Experiment (`ProbeGen/`): a probe generator ran in the same build as the Razor generator and reported what it saw.

- The trees in its input compilation were `Counter.razor.cs, ScriptComponent.cs, EmbeddedAttribute.cs, ValidatableTypeAttribute.cs, ...`,
  and no `*_razor.g.cs`.
- For `Counter`: `base=object interfaces=[IDisposable] trees=[Counter.razor.cs] members=[Dispose] attrs=[ObsoleteAttribute]`.
  That is only the code-behind partial: no `ComponentBase` base, no `@code` members.
- `Extra`, which has no code-behind, was "not in input compilation".

What follows for Fenrir's generators
([SystemGenerator.cs](../../../packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/SystemGenerator.cs) selects
`[System]`/`[StageSystem]` classes by syntax and checks `ISystem` on the symbol):

- `@attribute [System(...)]` or `@implements ISystem` in a `.razor` file are invisible to it, so a Razor component
  cannot be found as a system that way.
- A code-behind `partial class Foo : ISystem` with `[System]` in a `.cs` file is visible. The final compilation merges
  both halves.
- None of Fenrir's generators can generate code from a component's `[Parameter]` properties declared in `@code`, such
  as typed parameter setters. Parameters declared in a code-behind partial can be seen.
- The Razor generator in turn sees only user-written source, so Razor does not see Fenrir-generated members either,
  for example in `@code` type checks at tag-helper discovery. Attribute/parameter discovery runs on its declaration
  compilation.

## 2. Stock generator vs vendoring `Microsoft.AspNetCore.Razor.Language`

### 2.1 Licence and size

- **Licence:** dotnet/razor is MIT, "Copyright (c) .NET Foundation and Contributors"
  ([LICENSE.txt](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/LICENSE.txt)). The
  toolset package is MIT too (its nuspec, below).
- **dotnet/razor size at the pinned commit:** `src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src` has 622 `.cs`
  files, 3,119,156 bytes. That breaks down as `Language/` 539 files (2.84 MB), `CSharp/` 20, `SourceGenerators/` 14,
  and `Mvc*` 47. The shared `Microsoft.AspNetCore.Razor.Utilities.Shared` adds 104 files (0.66 MB) (git tree sizes via
  the GitHub API). The parser, intermediate passes and code writers that a fork edits are all in `Language/`.
- **Built size:** the SDK's compiled generator is 3.95 MB (`Microsoft.CodeAnalysis.Razor.Compiler.dll`) plus 0.55 MB
  (`Utilities.Shared.dll`).
- **s&box's fork:** `engine/Sandbox.Razor` is 435 `.cs` files, 2,097,955 bytes, at the pinned sbox commit. GitHub code
  search finds `garry:` edits in at least `MarkupElementIntermediateNode.cs`, `ComponentMarkupBlockPass.cs`,
  `DefaultDocumentWriter.cs` and `ComponentRuntimeNodeWriter.cs`. Code search is not exhaustive.

### 2.2 What a fork buys, with s&box's as the worked example

From the s&box note 1.2, and the files above:

- **Markup control:** chosen tags are never folded into HTML strings (1.6), so every element reaches the builder as calls.
- **`<style>`** becomes `AddStyleDefinitions(seq, css)`, not markup.
- **`Name:bind=@expr`** becomes `AddBind(seq, "Name", () => expr, v => expr = v)`: a getter and a setter. The stock
  compiler passes only the value (1.5).
- **Uppercase tags are panel classes,** so `<Healthbar>` would resolve without being an `IComponent` and without a
  `@using`.
- **Keys up front:** s&box's builder has `OpenElement(int sequence, string elementName, object key = null)`
  ([sbox RenderTreeBuilder.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd62bf7947129fa4f9d8f490d7580be7903/engine/Microsoft.AspNetCore.Components/RenderTreeBuilder.cs)),
  so the key arrives with the open rather than after the attributes. For components compiled by the stock SDK, s&box's
  bridge `SetKey` is a no-op (`public void SetKey( object value ) { }`, same file).
- **`AddLocation(file, line, column)`,** so every element knows its source line.
- **A render-tree checksum method** on each class, used to decide rebuilds on hot load.

### 2.3 Maintenance, and pinning without vendoring

- **Vendoring** freezes a compiler that is still moving. The razor repo merged changes as structural as removing
  `MetadataCollection` from tag helpers ([dotnet/razor#12123](https://github.com/dotnet/razor/pull/12123)) and
  `ItemCollection` ([#11939](https://github.com/dotnet/razor/pull/11939)). A fork also has to keep the IDE story
  working on its own: see 4.
- **The stock generator** follows the installed SDK. The `PlainSdk` wiring above, and the Razor SDK, both take the
  generator from `$(NETCoreSdkVersion)`'s folder, so the Razor language version moves with the SDK the repo's
  `global.json` selects.
- **Pinning a compiler independently of the SDK:**
  - [Microsoft.Net.Compilers.Razor.Toolset](https://www.nuget.org/packages/Microsoft.Net.Compilers.Razor.Toolset)
    exists, with versions `7.0.0-preview.23511.3`, `7.0.0-preview.23512.5` and `9.0.0-preview.25264.2` only (nuget.org
    flat-container index, 2026-11-14).
  - Its `9.0.0-preview.25264.2` nuspec says it causes "the project to be built using the Razor compilers and source
    generator contained in the package". It adds: "Using it as a long term solution ... is explicitly not supported"
    (nuspec read from the downloaded package). It contains `source-generators/Microsoft.CodeAnalysis.Razor.Compiler.dll`
    plus a `build/` targets file.
  - `Microsoft.CodeAnalysis.Razor.Compiler` on nuget.org has a single version, `10.0.0-preview.25277.114`.
  - `Microsoft.AspNetCore.Razor.Language` stopped at `6.0.36`.
  - So the options are: follow the SDK, reference a generator dll the repo carries (any copy of the SDK's
    `source-generators/` folder works as an `Analyzer`, as `PlainSdk` shows), or vendor the source.

## 3. Build time

Method (experiment: `time.sh`):

- Each variant was a separate project referencing the same stand-in project.
- Per run: `rm -rf bin obj` then `dotnet build` (clean, includes restore); `dotnet build --no-restore` again (no-op);
  append a line to one file; build again (edit).
- Six runs per variant, all after a warm-up, so the MSBuild node and VBCSCompiler server were warm. One extra clean
  build after `dotnet build-server shutdown` gave the cold figure.
- Wall clock in ms, from `date +%s%N`. The stand-in's up-to-date check is in every figure.

| Variant | Files | Clean min / median / max | No-op median | Edit median (file edited) | Cold clean |
| --- | --- | --- | --- | --- | --- |
| baseline, plain SDK, no Razor | 1 `.cs` | 1175 / 1208 / 3107 | 724 | 810 (`.cs`) | 2121 |
| `Microsoft.NET.Sdk.Razor` | 4 `.razor` + 1 `.cs` | 1347 / 1862 / 3767 | 844 | 935 (`.razor`) | 2555 |
| plain SDK + generator by hand | 4 `.razor` + 1 `.cs` | 1258 / 1339 / 1386 | 741 | 863 (`.razor`) | 2415 |
| `Microsoft.NET.Sdk.Razor` | 54 `.razor` + 1 `.cs` | 1796 / 1878 / 3908 | 859 | 1325 (`.razor`) | 3150 |

- **The generator's own time,** from `-p:ReportAnalyzer=true` on a clean build: "Total generator execution time: 0.068
  seconds" with 4 files, and 0.515 s with 54.
- **Reading the numbers:**
  - Plain SDK + generator costs about 0.1 s over the baseline on a clean build, and about 0.05 s on an edit.
  - The Razor SDK adds its static-web-assets targets: its no-op build is about 0.1 s slower than the plain variant's.
  - Editing one `.razor` in a 54-file project costs about 0.5 s more than in a 4-file one. The generator re-runs
    declaration generation and tag-helper discovery over the compilation; outputs for unchanged files are cached
    (`WithTrackingName` steps in [RazorSourceGenerator.cs](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/SourceGenerators/RazorSourceGenerator.cs)).
- **Caveats:** one desktop machine, no process isolation, Defender not excluded, and outliers in every column (the
  3-4 s maxima are single runs). The 54 files are 50 copies of one 40-line component. The scripts project is tiny, so
  real projects with more C# would show the Razor share smaller.

## 4. IDE support

Nothing here was run in an IDE; this section is from docs and from what the build shows.

- **Visual Studio (cohosted Razor editor).**
  - The razor repo's troubleshooting doc says the editor depends on the source generator: "Most issues with the
    cohosted editor stem from the Razor source generator not being enabled or not functioning correctly". It also says
    the generator "requires `.razor` and `.cshtml` files to be registered as additional items". As an alternative to
    the SDK defaults, it offers adding `<AdditionalFiles Include="**/*.razor" />` by hand
    ([docs/CohostingTroubleshooting.md](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/docs/CohostingTroubleshooting.md)).
    That is the plain-SDK wiring of 1.1.
  - Because the editor runs the same generator over the same references, completion and diagnostics for components,
    `[Parameter]`s and the directive attributes should follow the stand-in exactly. That includes 1.2's silent failures:
    a wrong assembly name or a missing `@using` would show plain HTML attributes, not errors. Not verified.
  - The Razor SDK also declares the `DotNetCoreRazor` and `DotNetCoreRazorConfiguration` project capabilities
    ([Microsoft.NET.Sdk.Razor.DesignTime.targets:31-37](https://github.com/dotnet/dotnet/blob/v10.0.402/src/sdk/src/RazorSdk/Targets/Microsoft.NET.Sdk.Razor.DesignTime.targets#L31-L37)),
    which the plain-SDK variant lacks. Whether VS needs them to open `.razor` in the Razor editor at all is not documented
    there; open question.
- **VS Code (C# extension / C# Dev Kit).** The C# extension is "the shipping vehicle for the VS Code Razor experience"
  ([docs/InsertingRazorIntoVSCodeCSharp.md](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/docs/InsertingRazorIntoVSCodeCSharp.md)).
  It is the same Razor language-server code as VS's, so the same dependency on the generator is expected. No doc was
  found on non-web projects specifically.
- **Rider.** Rider's Razor support is JetBrains' own (ReSharper) implementation, not the Roslyn generator, so the
  stand-in's behaviour there cannot be inferred from the above. JetBrains' support forum has reports of custom-component
  parameter completion missing in Rider while VS offers it
  ([Rider support thread](https://rider-support.jetbrains.com/hc/en-us/community/posts/14648647152914/comments/14750318867602)).
  The s&box wiki's Rider page suggests excluding s&box's `.razor` folders from inspection if they show errors
  ([S&box Wiki: Setting up Rider](https://wiki.facepunch.com/sbox/Setting_up_Rider)). Neither is a primary source for
  the stand-in case; open question.
- **What the experiment does show.**
  - `.razor` Content becomes `RazorComponent` items under the Razor SDK. Under the plain SDK, they are just `AdditionalFiles`.
  - Design-time builds skip markup-block folding and whitespace trimming: both passes return early when
    `Options.DesignTime` is set (1.6). The C# an editor sees for a component therefore differs from what the build emits.
  - Go-to-definition from a tag lands on the generated or declaration class. That is how Roslyn maps
    `#line (2,10)-(2,15) "...\Panel.razor"`; the generated files carry these mappings (1.5 excerpt, pragmas removed).

## 5. Hot reload

- **A rebuild-and-reload is nothing special for `.razor`.** The `.razor` files compile to ordinary classes in the
  scripts dll (1.5). Fenrir copies the rebuilt dll into a per-generation folder, releases the old collectible ALC, and
  loads the new one
  ([packages/FenrirScripting/README.md, "Hot reload"](../../../packages/FenrirScripting/README.md)). That works for these
  classes as for any others.
- **What a reload loses or must release** is whatever lives in managed objects of the old generation:
  - component instances and their fields (the `@code` state);
  - the delegates captured by `EventCallback`s and `@ref` closures;
  - and, if the builder's retained per-element caches live in the shared contract (default ALC), every entry in them
    that references an old-generation component or delegate. The unloadability doc lists exactly such references as
    what keeps a collectible ALC alive
    ([Learn: How to use and debug assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)).
- **Native elements survive** a reload, since they live in `UIRoot`. Whether the new generation adopts them or removes
  them is open (6.7).
- **Sequence numbers across reloads:** they are per-file compile-time counters (1.5, 6.1). An edited file's numbers
  shift, so reusing old elements keyed by `(sequence, loop)` across a reload is only sound for files whose generated
  code did not change. s&box gates this per class with `GetRenderTreeChecksum()`, a hash of the markup (s&box note 1.2).
  The stock generator emits `#pragma checksum` with a SHA-256 of the `.razor` source at the top of each generated file
  (1.5 excerpt header). That is a debugging checksum, not a runtime value; whether a runtime-readable checksum exists
  is an open question.
- **Metadata-update hot reload** (`dotnet watch`, Edit-and-Continue) is a different mechanism.
  - `MetadataUpdater.ApplyUpdate` throws `InvalidOperationException` when "The assembly is not editable"
    ([Learn: MetadataUpdater.ApplyUpdate](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.metadata.metadataupdater.applyupdate?view=net-10.0)).
  - In the v10.0.0 runtime, that is `!pModule->IsEditAndContinueEnabled()`
    ([assemblynative.cpp:1395-1399](https://github.com/dotnet/runtime/blob/v10.0.0/src/coreclr/vm/assemblynative.cpp#L1395-L1399)).
  - EnC is enabled on a module when the debugger asks for it, when forced by config, or when `DebugAssembliesModifiable`
    (the `DOTNET_MODIFIABLE_ASSEMBLIES=debug` switch) is on and JIT optimisations are disabled
    ([ceeload.cpp:517-525](https://github.com/dotnet/runtime/blob/v10.0.0/src/coreclr/vm/ceeload.cpp#L517-L525)).
  - The capability check excludes only optimised, system and Reflection.Emit modules, and says nothing about
    collectibility
    ([ceeload.cpp:540-557](https://github.com/dotnet/runtime/blob/v10.0.0/src/coreclr/vm/ceeload.cpp#L540-L557)).
    The unloadability doc's limitations list does not mention EnC either.
  - So nothing found rules `ApplyUpdate` out for an assembly in a collectible ALC. It would need the delta tooling to
    target Fenrir's private copy in a native-hosted process, and it was not tested.
  - Blazor's own hot reload re-renders through `HotReloadManager` and `MetadataUpdateHandler` hooks (for example,
    `ChangeDetection` clears its cache on `OnDeltaApplied`,
    [ChangeDetection.cs:13-19](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/ChangeDetection.cs#L13-L19)).
    That path is irrelevant to whole-assembly reload.

## 6. An in-place builder over FenrirUI's natives

Experiment: `Runner/InPlace.cs` is a ~250-line sketch of a `RenderTreeBuilder` subclass over a `FakeNatives` class.
The fake has one counter per `UIRoot` native it stands for, plus `LoadMarkup` standing for the C++ `UIRoot::LoadMarkup`,
which is not bound. `Runner.exe inplace` drives the generated `Counter` (with its child `Panel`) through twelve
rebuilds. It is a measurement device, not a design.

### 6.1 Keying: sequence number + loop occurrence, or sequence number + `@key`

- **Sequence numbers are compile-time literals.** The writer emits `.WriteIntegerLiteral(_sourceSequence++)` from a
  per-document counter
  ([ComponentRuntimeNodeWriter.cs:22](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentRuntimeNodeWriter.cs#L22),
  [:297-300](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentRuntimeNodeWriter.cs#L297-L300)),
  so each call site in the source has one fixed number. Every file starts at 0 (`Panel` and `Counter` both open with
  `OpenElement(0, "div")`).
- **Why that makes keying stable.** The number identifies the *source position*, not the execution order. An `@if`
  branch that disappears just leaves its number unused; it does not shift its siblings' numbers.
- **Blazor's docs on this:** sequence numbers "relate to code line numbers and not execution order". They also say
  "The necessary information doesn't exist to permit the framework to generate sequence numbers automatically at
  runtime unless the information is captured at compile time"
  ([Learn: Blazor advanced scenarios, sequence numbers](https://learn.microsoft.com/en-us/aspnet/core/blazor/advanced-scenarios?view=aspnetcore-10.0#sequence-numbers-relate-to-code-line-numbers-and-not-execution-order)).
- **Loops** repeat a number at runtime ("the numbers repeat is how the diffing system realises you're in a loop", same
  page). The builder therefore counts occurrences of each sequence number within its scope during a build. The n-th
  `OpenElement(11, "li")` under a given `<ul>` is the key `(11, n)`.
- **`@key`** replaces the occurrence count with the key value. The key is `(11, ("key", item.Id))`, so a removed,
  inserted or reordered item keeps its element.
- **`@key` comes late.** `SetKey` is emitted after the element's attributes: element keys are written after attribute
  children ([ComponentRuntimeNodeWriter.cs:209-212](https://github.com/dotnet/razor/blob/4fb38398b3058d2040cfdb033a1aae3945ddb78a/src/Compiler/Microsoft.CodeAnalysis.Razor.Compiler/src/Language/Components/ComponentRuntimeNodeWriter.cs#L209-L212);
  experiment: `SetKey(item.Id)` after `AddAttribute(12, "class", ...)`). An in-place builder cannot pick the element at
  `OpenElement`. The sketch buffers the open and its attributes, and resolves them at the first non-attribute call
  (`SetKey`, content, a child, or `CloseElement`). Blazor sets the key on the already-open frame
  ([RenderTreeBuilder.cs:524-545](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/Rendering/RenderTreeBuilder.cs#L524-L545)).
  s&box's fork avoids it by passing the key to `OpenElement` (2.2).
- **Scopes are regions, not just elements.** Three things nest inside an element: a child component's output, a
  `RenderFragment` (`ChildContent`, `@ItemTemplate(item)`, a templated `@<p>`), and the element's own children. The
  numbers in a fragment belong to the file that wrote it: the `ChildContent` lambda's 39-41 come from `Counter.razor`
  but run inside `Panel`. So each fragment and each component gets its own key region under the parent element. Blazor
  does the same: `AddContent(int, RenderFragment)` wraps the fragment in `OpenRegion`/`CloseRegion` "to indicate that
  the sequence numbers inside the fragment are unrelated to the sequence numbers outside it"
  ([RenderTreeBuilder.cs:104-116](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/Rendering/RenderTreeBuilder.cs#L104-L116)).
  s&box keys its blocks by a scope hash combining sequence and loop counter (s&box note 1.3,
  [RenderTree.Markup.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd62bf7947129fa4f9d8f490d7580be7903/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Markup.cs)).

### 6.2 Visited set and removal

- Each retained entry records the build number that last visited it.
- At an element's `CloseElement` (and at the end of the build for the root), the sketch removes every direct native
  child whose entry was not visited. Each takes one `UIRoot.Remove(element, RemoveMode.Outro)`, and its subtree goes
  with it ("Removes the element and everything under it: after its `:outro` transitions end", `UINative.cs`). That
  includes children produced through nested regions and components.
- s&box does the same with `Finish()` → `DestroyUnseen()` (s&box note 1.3).
- An element removed with `Outro` still sits among its parent's children until its transitions end. That affects the
  indices passed to later `Reorder`/`CreateElement` calls; whether `UIRoot` counts outgoing elements in indices was not
  checked (Open questions).

### 6.3 Attribute and text caching, mapped to natives

The sketch keeps the last string per attribute name per element, and calls a native only when it changed. The table
maps each kind of Razor input to the natives in `UIRoot.cs` / `UINative.cs`.

| Razor input | Arrives as | Native(s), only on change |
| --- | --- | --- |
| `class="a @(x ? "b" : "")"` | `AddAttribute(seq, "class", string)`, the whole string concatenated every build | split on whitespace, diff the token sets: `AddClass` / `RemoveClass` per token |
| `style="width: @(W)px"` | `AddAttribute(seq, "style", string)`, note `"width:" + " " + W + "px"` | split declarations, diff: `SetStyle(name, value)` per changed one; a dropped one needs a reset (the sketch passes `""`, but `SetStyle`'s reset semantics were not checked) |
| `id="x"` | `AddAttribute(seq, "id", string)` | `SetIdString` |
| `value` (incl. `@bind`'s `FormatValue`) | `AddAttribute(seq, "value", string/object)` | `SetValue(float)` for a slider/progress, `SetEntryText` for a `textentry`; the element's tag decides |
| `@onclick` etc. | `AddAttribute(seq, "onclick", EventCallback<T>)` | none: the handler is stored managed-side against the element; `UIEvent { Target, Kind }` from `EventReader<UIEvent>` is looked up there |
| other attributes | `AddAttribute(seq, name, string/object/bool)` | `SetAttribute(name, value)` |
| text runs | `AddContent(seq, string/object)`, several per element (`"Count: "`, `Count`) | concatenated per element, one `SetText` if the result changed; FenrirUI takes text only in `label`/`button` (1.6), so text elsewhere needs a generated `label` (as s&box does) or an error |
| static markup | `AddMarkupContent(seq, "<p>...</p>")`, a constant string | parsed once per `(seq, occurrence)`: needs `LoadMarkup` bound to C#; later builds only re-place it |
| `<style>` folded into markup | inside `AddMarkupContent` | `LoadMarkup` adds the sheet; per component instance that is one sheet per instance (open question) |
| `@ref` on an element | `AddElementReferenceCapture(seq, action)` | none: invoke `action` with a reference wrapping the `ElementId` |
| `@ref` on a component | `AddComponentReferenceCapture(seq, action)` | none |
| `@bind:event`, `@onclick:stopPropagation/preventDefault` | `SetUpdatesAttributeName`, `Add*Attribute` | none in the natives listed; managed metadata |

Blazor's attribute diff compares old and new frames by sequence and name, and the values with `Equals`
([RenderTreeDiffBuilder.cs:409-440](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/RenderTree/RenderTreeDiffBuilder.cs#L409-L440),
[:805-827](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/RenderTree/RenderTreeDiffBuilder.cs#L805-L827)).
An event handler keeps its id when the new value `Equals` the old one. `EventCallback` is a struct over receiver and
delegate, and `Delegate.Equals` compares target and method. So `() => Count++`, which captures only `this` in a lambda
the compiler emits as an instance method, compares equal across builds. A lambda capturing a loop variable gets a new
closure object each build and does not. s&box re-registers every listener every build (s&box note 1.3).

### 6.4 Child components and reordering

- **Child components.** `OpenComponent<T>(seq)` → find or create the instance at `(seq, occurrence or key)`. Then
  `AddComponentParameter(seq, name, value)` sets parameters by name; the sketch uses reflection, and Fenrir's generators
  cannot help here (1.7). `ChildContent` arrives through `AddAttribute`. `AddComponentReferenceCapture` hands out the
  instance, and `CloseComponent` runs the child's `BuildRenderTree` into its own region under the current element. The
  child has no element of its own: its root `<div>` is a child of the parent's element.
  - The sketch always re-runs the child.
  - Blazor re-renders a child only when `ChangeDetection.MayHaveChanged` says a parameter may have changed: values of
    known immutable types (primitives, string, `EventCallback`...) compared with `Equals`, and anything else assumed
    changed
    ([ChangeDetection.cs:21-52](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/ChangeDetection.cs#L21-L52)).
  - A `RenderFragment` parameter is a new delegate each build, so a child with `ChildContent` always re-renders in Blazor.
- **Reordering.** New elements are created at the end (`CreateElement(parent, tag, -1)`). At each element's close, the
  sketch compares the visited order with the native order it last left. It calls `Reorder(element, index)` only for
  children outside a longest increasing subsequence, so an insert or remove before a sibling costs nothing for that
  sibling. An earlier version that re-placed by absolute index paid 6 `Reorder`s for one `@if` insert (experiment, first
  `InPlace.cs` run). Blazor's diff gets the same effect from its sequence-ordered merge, with a keyed lookup when keys
  are present
  ([RenderTreeDiffBuilder.cs:43-290](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/RenderTree/RenderTreeDiffBuilder.cs#L43-L290)).

### 6.5 What a rebuild costs

Measured on `Counter` + `Panel`: 15 native elements, 2 of them markup blocks (experiment: `Runner.exe inplace`, output
in `razor-exp/inplace.txt`).

| Step | Native calls |
| --- | --- |
| 1. first build | 32: 13 `CreateElement`, 2 `LoadMarkup`, 4 `AddClass`, 8 `SetText`, 1 each `SetStyle`, `SetIdString`, `SetAttribute`, `SetValue`, `SetEntryText` |
| 2-3. rebuild, nothing changed | **0** |
| 4. `Count = 1` (h1 text, and `Panel`'s `Count` parameter → its text) | 2 `SetText` |
| 5. `Count = 5` (`@if` markup block appears) | 1 `LoadMarkup`, 1 `Reorder`, 2 `SetText` = 4 |
| 6. `Active = true` (class token) | 1 `AddClass` |
| 7. `Width = 120` (style declaration) | 1 `SetStyle` |
| 8. keyed item appended | `CreateElement` + `AddClass` + `SetText` = 3 |
| 9. first keyed item removed | 1 `Remove` |
| 10. two keyed items reversed | 1 `Reorder` |
| 11. `Count = 0` (`@if` block removed) | 1 `Remove`, 2 `SetText` = 3 |
| 12. nothing changed | 0 |

- **The managed side of a no-change rebuild still runs every `BuildRenderTree`.** That means building the `class` and
  `style` strings, boxing `int`s into `AddContent(int, object)`, allocating `EventCallback`s and closures, and the
  builder's lookups.
- **Measured:** the unoptimised sketch (dictionary-keyed regions, `MethodInfo.Invoke`, LINQ) took 12.3-12.6 µs and
  11,104 bytes per no-change build of this 15-element tree. That is Release, 20,000 builds after 2,000 warm-up. It is
  an upper bound for the approach, not an estimate for a tuned builder.

### 6.6 Compared with Blazor and s&box

- **Blazor** keeps two frame arrays (old and new render trees) per component. `RenderTreeDiffBuilder` walks them in
  step:
  - it matches by sequence number;
  - it detects loops when sequence numbers go backwards;
  - it prefers keys when either side has one;
  - and it emits an edit script (`SetAttribute`, `UpdateText`, insert/remove frame...) that the renderer applies to
    the DOM
    ([RenderTreeDiffBuilder.cs:43-290](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/RenderTree/RenderTreeDiffBuilder.cs#L43-L290)).
  - Text and markup frames are compared by string
    ([:580-600](https://github.com/dotnet/aspnetcore/blob/v10.0.0/src/Components/Components/src/RenderTree/RenderTreeDiffBuilder.cs#L580-L600)).
  - The cost is building a full frame array every render, then a linear diff.
- **s&box** has no frame array. `PanelRenderTreeBuilder` mutates panels while `BuildRenderTree` runs, keyed by
  `HashCode.Combine(loop, sequence)` blocks, and destroys unseen blocks at `Finish()` (s&box note 1.3,
  [RenderTree.Markup.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd62bf7947129fa4f9d8f490d7580be7903/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Markup.cs)).
  It caches attributes with a hash, `CheckCacheValue`, and parses markup blocks once.
- **The sketch here is s&box's model**, with the retained state keyed per region and the side effects going through
  `UIRoot` handles instead of `Panel` objects. Every native call is an interop transition into C++. That is why the
  per-attribute caching (zero calls when unchanged) carries more weight than it does for s&box, whose panels are
  managed objects.

### 6.7 Options the evidence leaves open (not decisions)

- **Where the builder's retained state lives:**
  - in the shared contract (default ALC), where reload must purge it (5);
  - or in the scripts generation, where reload drops it and native elements must be adopted or removed.
- **What triggers a rebuild:**
  - every frame, which is cheap natively but costs managed time as 6.5 shows;
  - a dirty flag set by handlers, as s&box's `StateHasChanged()` does;
  - a hash, as s&box's `BuildHash()` does;
  - or after any `UIEvent` that dispatched to a handler.
- **How static markup is realised:**
  - bind `LoadMarkup` to C#;
  - parse in C#;
  - force C# into every element (1.6);
  - or fork the compiler.
- **How `@bind` maps to controls:** `BindAttributes` rules per FenrirUI tag (`slider`, `checkbox`, `textentry`); the
  change event comes from `UIEventKind.Change` with `Value`.
- **How components get parameters:** reflection (Blazor's approach, cached), parameters in code-behind partials that
  a Fenrir generator can see, or a fork that emits typed setters (s&box's `AddAttribute<T>(seq, Action<T>)` style).

## Open questions

- **Untested directives:** `@page`/`RouteAttribute`, `@layout`, `@inject`, `@rendermode`, `@formname`, `@bind:get`/`@bind:set`/`@bind:after`
  (which use `CreateInferredBindSetter`/`InvokeSynchronousDelegate`), `CascadingTypeParameter`, and `MarkupString` content.
  What do they need from the stand-in, and do any fail silently?
- **VS without capabilities:** does Visual Studio open `.razor` in the cohosted Razor editor for a plain-SDK project
  without the `DotNetCoreRazor` capability? Does completion follow the stand-in's `EventHandlers`/`BindAttributes`?
  What does Rider do with a stand-in named `Microsoft.AspNetCore.Components`?
- **Name clash with the real framework:** if a host's runtimeconfig ever includes `Microsoft.AspNetCore.App`, the real
  `Microsoft.AspNetCore.Components.dll` (version 10.0.0.0) and the stand-in would compete for the same simple name in
  the default context. Which wins, and does it matter for a native host that loads only `Microsoft.NETCore.App`?
- **Outro and indices:** does `UIRoot` count an element under `Remove(..., Outro)` in sibling indices until its
  transitions end? This decides whether `Reorder`/`CreateElement` indices computed by a builder stay correct during
  outros.
- **`<style>` per instance:** a `<style>` folded into a component's markup would be loaded once per component
  instance by `LoadMarkup`. Is a duplicate sheet harmful, or deduplicated by name?
- **Entities:** which entities does `ComponentMarkupEncodingPass` emit in markup strings, beyond the five XML ones and
  numeric references that FenrirUI's markup decodes?
- **A runtime checksum:** is there a stock-generator output usable at runtime to tell whether a class's generated tree
  changed across a reload, other than the `#pragma checksum` line? Or would it need a Fenrir generator hashing the
  `.razor` `AdditionalFiles` itself, as s&box's `RazorGenerator` does (s&box note 1.2)?
- **`ApplyUpdate` in a collectible ALC:** does it work for an assembly in a collectible ALC in a native-hosted runtime
  with `DOTNET_MODIFIABLE_ASSEMBLIES=debug`? It was not tested, and nothing found rules it out.
- **Tuned managed cost:** what does a no-change rebuild cost with a builder that avoids dictionaries and reflection?
  Generic `AddContent<T>` overloads would avoid boxing, and interpolated-string handlers would cover `class`/`style`.
