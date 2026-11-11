# How s&box builds game UIs: from Razor panels down to the pixels

Research note, 2026-11-10. Primary sources only: the public engine source
([Facepunch/sbox-public](https://github.com/Facepunch/sbox-public), pinned at commit
[`2643ebd6`](https://github.com/Facepunch/sbox-public/commit/2643ebd62bf7947129fa4f9d8f490d7580be7903), master on
2026-11-10), the official docs at sbox.game/dev/doc (read as raw Markdown by appending `.md`, as the repo's
[AGENTS.md](https://github.com/Facepunch/sbox-public/blob/2643ebd6/AGENTS.md) recommends), Facepunch's dated news
posts on sbox.game, and Valve's Panorama pages on the Valve Developer Community wiki. Repo links below point at the
pinned commit; line numbers are from that commit. Everything not verifiable against those sources is marked as such.

## Summary

1. A s&box UI is a tree of C# `Panel` objects. Razor is a convenience syntax: the docs say "They don't use a HTML renderer. This syntax is a convenience." A whole UI can be built in plain C# (`new Label { Parent = this }`), and the engine's own dev tools are.
2. `.razor` files are compiled by a vendored, Facepunch-modified copy of `Microsoft.AspNetCore.Razor.Language` into C# (`BuildRenderTree`), then by Roslyn with the rest of the game code; a tiny stand-in `Microsoft.AspNetCore.Components` assembly replaces Blazor's runtime.
3. Unlike Blazor, there is no virtual render tree or diff: `PanelRenderTreeBuilder` creates, updates and destroys real panels in place, keyed by Razor sequence numbers; a rebuild happens when `BuildHash()` changes, a bind changes, `StateHasChanged()` is called, or a mouse event (other than move) hits a panel.
4. Styling is an SCSS subset parsed in C# (`$variables`, `@import`, `@mixin`/`@include`, nesting, `@keyframes`, transitions, pseudo-classes incl. `:intro`/`:outro`), with a documented property list that is close to web CSS plus game extras (`sound-in`, `border-shape`, `text-stroke`, `backdrop-filter`).
5. Layout is a pure-C# library, `Sandbox.Layout` (flex derived from Yoga 3.2.1, plus block, grid and inline text). Yoga itself was used from December 2020 until it was replaced on 2026-09-05.
6. Rendering is retained-in-C#, regenerated every frame: each `RootPanel` records its tree into a `CommandList` through a `Painter`, whose `PainterBatcher` fills per-frame GPU tables (box instances, scissors, transforms, gradients, shapes, paths, glyphs) and issues one instanced quad draw per batch with `ui_cssbox_batched.shader`.
7. Rounded corners, borders, border-images, gradients, polygon/circle shapes, strokes, shadows, scissoring and text are all evaluated analytically in that shader; text glyphs are quadratic Bézier outlines uploaded once per process (fonts via SkiaSharp, shaping via a vendored RichTextKit); layers (filters, masks, opacity groups) and backdrop blur use render targets and frame grabs.
8. The same `Painter` is a public immediate-mode-style API for panels (`OnDraw(Painter)`), camera HUD/overlay, textures and command lists; the older `HudPainter`/`Panel.Draw` are obsolete wrappers over it, and `IPanelDraw` lets a panel append raw `CommandList` commands.
9. Nothing UI-specific crosses into native: the native Source 2 core receives GPU buffers, render attributes, materials, render targets and `DrawIndexedInstanced` calls through generated interop; no Panorama symbol appears in the managed tree or the interop definitions.
10. Panorama (XML/CSS/JS, flow-children layout, 1080p-relative scaling) was replaced by the C# system in December 2020; s&box kept the panel/CSS/1080p ideas and the custom-property habit, replaced XML/JS with Razor/C# and flow layout with flexbox, and dropped Panorama's non-standard semantics.

## 1. Authoring: Razor panels and the non-Razor path

### 1.1 What a `.razor` panel is

The docs describe a panel component as a `.razor` file with `@inherits PanelComponent`, a `<root>` element holding
markup with embedded C#, and an `@code` block; its `BuildHash()` override decides when the tree is rebuilt
([Razor Panels](https://sbox.game/dev/doc/ui/razor-panels.md)). The same page states the core design position:
"The panels are created and rendered exactly the same way. They don't use a HTML renderer. This syntax is a
convenience." A child `.razor` file without `@inherits` becomes a `Panel` subclass; the vendored compiler sets the
default base type to `global::Sandbox.UI.Panel`
([ComponentDocumentClassifierPass.cs:76](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/Components/ComponentDocumentClassifierPass.cs#L76)).

Documented authoring features ([Razor Panels](https://sbox.game/dev/doc/ui/razor-panels.md),
[Razor Components](https://sbox.game/dev/doc/ui/razor-panels/razor-components.md)):

- `<root>` is optional; without it every element is a child of the panel. `@foreach`, `@if` and early `return` work inside markup.
- Child panels are used as tags (`<MyChildPanel Health=@(30) />`); parameters are plain C# properties.
- `@ref="PanelReference"` stores the created panel in a field.
- Two-way binding is `Value:bind=@IntValue` (note `:bind`, not Blazor's `@bind-Value`).
- `RenderFragment` properties give slots; every panel has a built-in `ChildContent` fragment; `RenderFragment<T>` with `Context` and `@typeparam` generics are supported.
- `PanelComponent` differs from `Panel`: it is a scene `Component` on a `GameObject`, exposes its panel through `.Panel`, has `OnTreeFirstBuilt`/`OnTreeBuilt`, and cannot be used as a tag inside another panel; a `Panel` has `OnAfterTreeRender(bool firstTime)` and `Tick()` instead of `OnStart`/`OnUpdate`.
- Rebuild triggers: `BuildHash()` change, `StateHasChanged()` (queued for next frame), and "A Panel's contents will also rebuild if it has `pointer-events` and the cursor enters/exits/clicks the Panel."

### 1.2 How `.razor` becomes C#

Game code is compiled in-engine. `Compiler.ProcessRazorFiles` takes every `.razor` additional file of a code
archive, calls `Sandbox.Razor.RazorProcessor.GenerateFromSource`, and parses the result into a Roslyn syntax tree named
`_gen_{file}_{hash}.cs`; unchanged files reuse their previous tree, the generated path is mapped back to the `.razor`
file for debugging, and duplicates raise `SB6001`
([Compiler.Razor.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Compiling/Compiler/Compiler.Razor.cs)).
`RazorProcessor` injects an `@namespace` derived from the folder path, then runs the Razor project engine on the
source as `FileKinds.Component`
([Razor.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Razor.cs)).

The Razor engine is a vendored copy of `Microsoft.AspNetCore.Razor.Language` living in
[`engine/Sandbox.Razor`](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Razor), with
Facepunch edits marked by `// garry:` comments. The visible modifications:

- Tags whose first letter is uppercase are treated as panel classes
  ([MarkupElementIntermediateNode.cs:29](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/Intermediate/MarkupElementIntermediateNode.cs#L29)).
- `<style>` blocks are not HTML: their text is emitted as `__builder.AddStyleDefinitions( seq, "..." )`
  ([ComponentRuntimeNodeWriter.cs:157-161](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/Components/ComponentRuntimeNodeWriter.cs#L157-L161),
  [ComponentMarkupBlockPass.cs:160-164](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/Components/ComponentMarkupBlockPass.cs#L160-L164)).
- `Name:bind=@expr` is emitted as `AddBind( seq, "Name", () => expr, ( v ) => expr = v )`
  ([ComponentRuntimeNodeWriter.cs:380-410](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/Components/ComponentRuntimeNodeWriter.cs#L380-L410)).
- Source positions are emitted as `AddLocation( file, line, column )` so every panel knows the file and line it came from
  ([CodeWriterExtensions.cs:487](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/CodeGeneration/CodeWriterExtensions.cs#L487);
  `Panel.SourceFile`/`SourceLine` in [Panel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.cs)).

The runtime side of Blazor is not used. `engine/Microsoft.AspNetCore.Components` is a five-file stand-in
(`EventHandlers.cs`, `RenderFragment.cs`, `RenderHandle.cs`, `RenderTreeBuilder.cs`, `RuntimeHelpers.cs`) whose
`RenderTreeBuilder` is abstract and declares the s&box-specific calls (`AddLocation`, `AddStyleDefinitions`,
`AddBind`, `OpenElement<T>`, `AddReferenceCapture`, `SetRenderFragment`) plus bridges so components compiled by the
stock Razor SDK also work (`OpenComponent<T>`, `AddComponentParameter`, and a no-op `SetKey`)
([RenderTreeBuilder.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Microsoft.AspNetCore.Components/RenderTreeBuilder.cs),
[Microsoft.AspNetCore.Components.csproj](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Microsoft.AspNetCore.Components/Microsoft.AspNetCore.Components.csproj)).

Engine projects that use the stock Razor SDK get the missing pieces from a Roslyn incremental generator: for each
`.razor` it emits a partial class with `[ClassFileLocation("/path/File.razor")]` (how the runtime finds the sibling
`.razor.scss`) and a `GetRenderTreeChecksum()` returning the first 8 bytes of the markup's SHA-256, which "marks it as
having a razor tree at all" and "Changes when the markup changes, so the runtime knows to rebuild the tree on hotload"
([RazorGenerator.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Generator/RazorGenerator.cs)).

### 1.3 How the generated tree becomes panels (the Blazor difference)

Blazor's generated `BuildRenderTree` normally writes into a frame array that a renderer diffs. In s&box the builder
is `PanelRenderTreeBuilder`, and it mutates real panels while the Razor code runs
([RenderTree.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.cs),
[RenderTree.Block.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Block.cs),
[RenderTree.Panels.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Panels.cs)):

- Each `OpenElement(sequence, name)` pushes a scope whose hash combines the Razor sequence number and a loop counter; the hash selects a `Block` that owns one panel. `FindOrCreateElement` makes `div`/`p`/`span` plain `Panel`s and anything else through `TypeLibrary.Create<Panel>( elementName )` (so `<label>` and `<MyPanel>` both resolve through the type library); the panel's `ElementName` is the tag name and it is re-parented and re-indexed to its current position.
- `Start()` marks the root block seen and `Finish()` calls `DestroyUnseen()`: panels whose block was not visited this build are deleted (with outro transitions unless immediate). This is the "diff": reconcile-by-key against the live tree, not a frame comparison.
- Text content between tags is flushed into a generated `Label` (`IsGeneratedText = true`) via `SetContent`.
- Attributes go through `CheckCacheValue` (hash of element + attribute + value) so unchanged attributes are not re-set; `class`, `style`, `id`, `value` are special-cased in `Panel.SetProperty`, and other names are set by reflection through the type library
  ([Panel.Property.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Property.cs)).
- `<style>` blocks become a `StyleSheet.FromString` sheet named `__Razor.{hash}` added to the panel's stylesheet collection.
- `@onclick=...` becomes `RemoveEventListener( "onclick" )` + `AddEventListener( "onclick", action )` on every build ("There's no real good way to cache this")
  ([RenderTree.Events.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Events.cs)).
- `@ref` is `AddReferenceCapture`, which also registers a clearer that nulls the field when the block is destroyed.
- `:bind` creates a closure per bind that compares value hashes in both directions; `UpdateBinds()` runs them each tick and returns true if any changed, which dirties the tree
  ([RenderTree.Panels.cs `AddBind`](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Panels.cs)).

The per-panel driver is in `Panel.Razor.cs`: `StateHasChanged()` sets `razorTreeDirty`; `BuildHash()` defaults to 0
and is compared each tick; `GetRenderTreeChecksum()` (overridden by the generator) decides whether a panel has a
tree at all; `InternalRenderTree()` runs `BuildRenderTree` between `Start()`/`Finish()`; `OnAfterTreeRender(firstTime)`
is the post-build hook
([Panel.Razor.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Razor.cs)).
The order inside a frame is in `Panel.TickInternal`: parent-change handling, `InternalTreeBinds()` (BuildHash and
binds), rebuild if dirty, `::before`/`::after` upkeep, scrollbars, children ticked (reverse order), deferred
`OnAfterTreeRender`, pending events, then the user's `Tick()`
([Panel.cs:310-420](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.cs#L310-L420)).
Mouse events other than `onmousemove` set `razorTreeDirty = true` on the panel they reach
([Panel.Event.cs:214-250](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Event.cs#L214-L250)),
which is the mechanism behind the documented "rebuild on enter/exit/click".

`PanelComponent` wraps all of this in a scene component: `EnsurePanelCreated` builds a file-local `CustomBuildPanel`
whose `BuildRenderTree`/`OnAfterTreeRender`/`GetRenderTreeChecksum`/`BuildHash` forward to the component, loads the
sibling `.scss` through `ClassFileLocationAttribute`, and parents the panel to the `IRootPanelComponent`
(`ScreenPanel` or `WorldPanel`) on the same object or the nearest enabled `IPanelComponent` ancestor
([PanelComponent.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Scene/Components/UI/PanelComponent.cs)).

### 1.4 The non-Razor path

The docs' first example is code-only: a `Panel` subclass that creates a `Label`, sets `MyLabel.Parent = this`, and
updates `MyLabel.Text` in `Tick()`; a code-only root is a `PanelComponent` that creates children in
`OnTreeFirstBuilt` with `myPanel.Parent = Panel` ([UI](https://sbox.game/dev/doc/ui.md)). The API surface for this:

- Constructors `Panel()`, `Panel( Panel parent )`, `Panel( Panel parent, string classnames )`; the `Parent` setter re-parents, and `Label`/`Image` cannot have children (a child parented to one becomes its sibling)
  ([Panel.cs:115-141](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.cs#L115-L141),
  [Panel.Children.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Children.cs)).
- `panel.AddChild<T>()` and the `panel.Add` builder (`PanelCreator`, extended by extension methods per control)
  ([PanelCreator.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Utility/PanelCreator.cs)).
- `AddClass`/`SetClass`/`Classes`, `Style.X = ...`, `StyleSheet.Load(path)`, `AddEventListener( "onclick", ... )`, and overriding `OnClick`, `OnMouseDown`, `Tick`, `OnLayout`, `OnDraw( Painter )`.
- A control library in `engine/Sandbox.Engine/Systems/UI/Controls` (`Label`, `Button`, `Image`, `Checkbox`, `DropDown`, `TextEntry`, `SliderControl`, `ScrollBar`, `TabPanel`, `TreeView`, `Table`, `Popup`, `Menu`, `SplitContainer`, `CurveEditor`, `ScenePanel`, `SvgPanel`, `WebPanel`, and more)
  ([Controls directory](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/UI/Controls)).
  `Button` is a code-only control that carries its SCSS in a `[StyleSheet.Inline]` attribute
  ([Button.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Controls/Button.cs)).

Yes, a full UI can be built without Razor: the engine's own in-game dev tools are plain C# panels
([Systems/DevTools/UI](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/DevTools/UI)),
as are the `PanelGallery` launcher's pages and a mock editor built from panels in OS windows
([Launcher/PanelGallery](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Launcher/PanelGallery)),
while the gallery's CSS conformance tests are `.razor` files. The shipped `game/addons` tree holds 225 `.razor`
files, so Razor is the common path for game-facing UI
(count from `git ls-tree` at the pinned commit).

## 2. Styling and layout

### 2.1 The stylesheet language

The parser is hand-written C# in `Systems/UI/Parser`. It supports `$variable` definitions (`!default` honoured) with
whole-token substitution, `@import` (a bare name resolves to `_name.scss`, with loop protection), `@mixin` with
parameters, defaults, variadics and `@content` blocks, `@include` at top level or inside rules, nested rules
(`> .child`, `&.state`), and skips unknown at-rules such as `@media` or malformed rules with a warning instead of
dropping the sheet ("recover" mode)
([StyleParser.Sheet.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Parser/StyleParser.Sheet.cs),
[Mixin.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Parser/Mixin.cs),
[StyleParser.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Parser/StyleParser.cs);
nesting examples in [Button.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Controls/Button.cs)).
A `StyleSheet` holds `Nodes` (rule blocks bucketed by class for fast matching), `Variables`, `KeyFrames` and
`Mixins` ([StyleSheet.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/StyleSheet.cs)).
`@keyframes` is modelled by `KeyFrames` (interval 0..1 blocks of `Styles`, interpolated per frame)
([KeyFrames.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Animation/KeyFrames.cs),
[Styles.Animation.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/Styles.Animation.cs)).
Transitions are per-panel `Transitions.Entry` records (property, start time, length, easing function) applied to
the computed style each frame
([Transitions.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/Transitions.cs)).

Pseudo-classes the selector parser accepts: `:hover`, `:active`, `:focus`, `:focus-within`, `:root`, `:disabled`,
`:enabled`, `:checked`, `:intro`, `:outro`, `:empty`, `:first-child`, `:last-child`, `:only-child`,
`:nth-child(n | odd | even)`, `:has(...)`, and the pseudo-elements `::before`/`::after`
([StyleParser.Selector.cs:258-367](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Parser/StyleParser.Selector.cs#L258-L367),
[StyleSelector.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/StyleSelector.cs)).
`::before`/`::after` are real child panels created and destroyed during tick
([Panel.BeforeAfter.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.BeforeAfter.cs)).
`:intro` is on at creation and switched off after the panel's first final layout; `:outro` is added by
`Delete()` and the panel is deleted after its transitions finish
([Panel.Layout.cs:505-512](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Layout.cs#L505-L512),
[Panel.Delete.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Delete.cs),
[Style Properties](https://sbox.game/dev/doc/ui/styling-panels/style-properties.md)).

The property set is generated: a T4 template lists every property with its C# type, default and whether it inherits
(`FontSize` 13px inherited, `PointerEvents` none inherited, `JustifyContent` stretch, ...)
([BaseStyles.Generated.tt](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/BaseStyles.Generated.tt)).
The documented list ([Style Properties](https://sbox.game/dev/doc/ui/styling-panels/style-properties.md)) covers:
flex alignment and sizing (`align-*`, `justify-content`, `flex-*`, `gap`, `order`), `display: flex | none | contents`,
`position: static | relative | absolute` (plus fixed overlays in code), box sizing and logical `inset-*`/`margin-block`
etc., borders, `border-radius` (elliptical), `border-image` (9-slice), `outline`, backgrounds with `linear-`,
`radial-` and `conic-gradient()`, `background-clip: text`, `background-blend-mode`, fonts and text (weights, `text-stroke`,
`text-shadow`, `text-decoration-*`, `font-variant-numeric`, `white-space`, `word-break`, `text-overflow`), `opacity`,
`box-shadow`, `mix-blend-mode`, `filter` (blur/brightness/contrast/hue-rotate/invert/saturate/sepia/tint/drop-shadow/border-wrap),
`backdrop-filter`, `mask-image`/`mask-mode`/`mask-repeat`, scrollbars, `transform` (with `perspective-origin`),
transitions (timing functions include `bounce-*`, `sin-ease-*`, `cubic-bezier()`, `steps()`), animations, and CSS-wide
keywords with `oklch()`/`lab()`/`hwb()` colours, `min()`/`max()`/`clamp()`. Game-specific additions on the same page:
`aspect-ratio`, `background-tint`, `background-playback-state` (video backgrounds), `border-shape: polygon(...) | circle(...)`
(also used for hit-testing, [Border Shape](https://sbox.game/dev/doc/ui/styling-panels/border-shape.md)), `mask-scope`,
`sound-in`/`sound-out`, `text-filter`, `text-stroke`. Properties marked as behaving differently from the web:
`display`, `font-family` (a single family name), `position`, `scrollbar-color`, `scrollbar-width`. CSS custom properties
(`--x`) are not mentioned anywhere in the docs or parser; `$` SCSS variables are the variable mechanism.

### 2.2 Attaching styles to panels

Resolution order in `Panel.LoadStyleSheet()`: `[StyleSheet.Inline(name, scss)]` attributes on the type and its
bases (shared, parsed once per key); then a `[StyleSheet("path.scss")]` attribute (relative to the class file or
absolute); then the automatic sibling file `<ClassFile>.scss` found through `ClassFileLocationAttribute`
([Panel.StyleSheet.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.StyleSheet.cs)).
The docs describe the same three plus `<style>` blocks in Razor, `style="..."` attributes with embedded C#, and
`panel.Style.Width = Length.Percent(...)` from code ([Styling Panels](https://sbox.game/dev/doc/ui/styling-panels.md)).
Every panel owns a `StyleSheetCollection`, and matching considers the sheets of the panel and all ancestors
(`AllStyleSheets`, [Panel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.cs)).
A `RootPanel` loads `/styles/base/rootpanel.scss` as its base sheet
([RootPanel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/RootPanel.cs)).

### 2.3 Hotloading

- SCSS: `StyleSheet.FromFile` attaches a `FileWatch` on the mounted filesystem; on change the sheet re-parses itself, re-watches everything it `@import`ed, and calls `UISystem.DirtyAllStyles()`
  ([StyleSheet.cs:235-262](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/StyleSheet.cs#L235-L262)). Inline sheets re-parse when their string changes (a code hotload).
- Razor and C#: saving recompiles the project and live-reloads the assembly, walking the heap to upgrade instances; method-body-only edits use a faster IL patch ([Hotloading](https://sbox.game/dev/doc/code/advanced-topics/hotloading.md)). After a hotload `GlobalContext` calls `UISystem.OnHotload()`, and `Panel.OnHotloaded()` reloads stylesheets, re-initialises `[PanelEvent]` listeners, prunes children whose types vanished, and if `GetRenderTreeChecksum()` changed, clears and rebuilds the Razor tree
  ([Panel.cs:156-196](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.cs#L156-L196),
  [GlobalContext.cs:310](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Core/GlobalContext/GlobalContext.cs#L310)).
- Fonts: `FontManager` watches `/fonts/*.ttf|*.otf` and reloads
  ([FontManager.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/TextRendering/FontManager.cs)).
- Historical note: live stylesheet edits on a listen server were pushed to clients and hotloaded there as early as March 2021 ([March 2021 news, "File Transfers"](https://sbox.game/news/march2021/css-rendering-improvements)).

### 2.4 Layout engine

Layout is `Sandbox.Layout`, described in its project file as "Managed UI layout engine (flexbox, block, grid). Pure
C#, no native or engine dependencies - Sandbox.Engine drives it from PanelLayout"
([Sandbox.Layout.csproj](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Layout/Sandbox.Layout.csproj)).
Its `Display` enum is `Flex, None, Contents, Block, Grid, Inline`
([Enums.cs:81-89](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Layout/Styles/Enums.cs#L81-L89));
a `LayoutNode` has a `LayoutStyle`, children, an optional measure function for leaves and a baseline function, and
"Styles and structure changes mark the path to the root dirty; a clean root means the whole tree is clean"
([LayoutNode.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Layout/LayoutNode.cs)).
The flex algorithm carries Yoga's MIT notice: "Portions of the flex layout implementation and converted flex
conformance tests derive from Yoga 3.2.1"
([FlexLayout.cs:1415-1440](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Layout/Layout/Flex/FlexLayout.cs#L1415-L1440));
block/grid fixtures come from Taffy
([Layout/Tools/README.md](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Tests/Sandbox.Test.Unit/Layout/Tools/README.md)).
History: the December 2020 rewrite said "The layout system is now based on yoga, so it's all flex based"
([December 2020 news, "Replacing Panorama"](https://sbox.game/news/dec2020/replacing-panorama)); the first commit
touching `engine/Sandbox.Layout` in the public history is
[`a7b51476`](https://github.com/Facepunch/sbox-public/commit/a7b51476) (2026-09-05, "Replace Yoga with Sandbox.Layout:
CSS block, grid and inline text (#5711)"). So yogalayout.dev is relevant only to the pre-2026-09 engine; the
current engine runs a C# port with extensions.

`PanelLayout` is the bridge: it converts computed `Styles` into `LayoutStyle` values, tracks which reference sizes
(parent width/height, font size, root font size, viewport) a panel's lengths depend on, and reads back the rect,
margin, padding and border
([PanelLayout.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Layout/PanelLayout.cs)).
Leaves supply measure functions: `Label` measures text, `Image` measures its texture
([Label.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Controls/Label.cs),
[Image.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Controls/Image.cs)).

Per frame, `UISystem.Simulate` runs `TickPanels`, `TickInput`, then `LayoutAndBuild` = `PreLayout` (style cascade:
`Style.BuildFinal` per panel, with rule re-evaluation parallelised through `JobDispatcher` when 32+ panels are
pending), deferred deletion, `Layout` (`LayoutTree.CalculateLayout()` only if the root is dirty), `PostLayout`
(`FinalLayout` writes `Box.Rect/RectInner/RectOuter/ClipRect`, floored to pixels, and calls `OnLayout`), then
`BuildCommandLists` and `CombineCommandLists`
([UISystem.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.cs),
[RootPanel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/RootPanel.cs),
[Panel.Layout.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Layout.cs)).
Root scale defaults to `screenHeight / 1080` (handheld x1.333, VR special-cased), and `ScreenPanel` offers
`ConsistentHeight` or `FollowDesktopScaling`
([RootPanel.cs `UpdateScale`](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/RootPanel.cs),
[ScreenPanel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Scene/Components/UI/ScreenPanel.cs),
[UI](https://sbox.game/dev/doc/ui.md)).

## 3. The panel model

### 3.1 Types

- `Panel` (`[Library("panel"), Alias("div","span")]`): `ElementName`, `Id`, classes, `PseudoClass` flags, `StyleSheet` collection, `Style` (authored) vs `ComputedStyle`, `Box` rects, `Parent`/`Children`, `Tick()` ("Called every frame. This is your 'Think' function"), `OnParentChanged`, `WantsMouseInput` (true if `pointer-events: all` on self or a child)
  ([Panel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.cs)).
- `RootPanel : Panel`: `PanelBounds`, `Scale`, `RenderedManually`, `IsWorldPanel`, `IsVR`, its own `PanelCommandList` ("Single flat command list used by the flat rendering path. All panels record into this one list"), style-rule rebuild list, Tab focus traversal
  ([RootPanel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/RootPanel.cs)).
- `Sandbox.UI.WorldPanel : RootPanel`: owned `ScenePanelObject` (a `SceneCustomObject` in a `SceneWorld`), default bounds 1000x1000 centred, `Scale = 2`, `ScreenToWorldScale = 0.05`, `MaxInteractionDistance`, `Lighting`
  ([WorldPanel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/WorldPanel/WorldPanel.cs),
  [ScenePanelObject.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/WorldPanel/ScenePanelObject.cs)).
- Scene components: `ScreenPanel` (an `IRootPanelComponent`) creates a `GameRootPanel` with `RenderedManually = true`, exposes `Opacity`, `Scale`, `AutoScreenScale`, `ZIndex`, `TargetCamera` and `Timing` (`AfterPostProcess` or `BeforePostProcess`, the latter letting bloom/tonemapping affect the UI)
  ([ScreenPanel.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Scene/Components/UI/ScreenPanel.cs));
  the `WorldPanel` component is a `Renderer` that creates a `Sandbox.UI.WorldPanel` with `PanelSize`, alignment, `RenderScale`, `LookAtCamera`, `Lighting`, `InteractionRange`
  ([WorldPanel.cs (component)](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Scene/Components/UI/WorldPanel.cs));
  `PanelComponent` is described in section 1.3. A `WorldInput` component drives ray-based interaction with world panels
  ([UISystem.cs `TickWorldInput`](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.cs)).
- `UISystem` (one per `GlobalContext`, and one per `UISurface` for editor windows): root list, `PanelInput`, `InputEventQueue`, focus, tooltips, popup host, deferred deletion
  ([UISystem.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.cs),
  [UISurface.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Surface/UISurface.cs)).

### 3.2 Events and input

- Mouse: `PanelInput.Tick` hit-tests each root (topmost first) with the cursor (a virtual cursor is synthesised from a gamepad stick when enabled), tracks `Hovered`, `Active` (pressed) and drag/drop targets, and switches `:hover`/`:active` pseudo-classes
  ([PanelInput.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Input/PanelInput.cs)).
  Hit-testing honours rounded corners and `border-shape` on the CPU (`Panel.IsInside`)
  ([Panel.Input.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Input.cs)).
- Dispatch: `Panel.OnEvent(PanelEvent)` maps names to virtuals (`onclick` -> `OnClick`, `onmousedown`, `onmouseup`, `ondoubleclick`, `ontripleclick`, `onmousemove`, `onmouseover`, `onmouseout`, `ondrop`, `ondragenter`, `onfocus`, `onblur`, `onback`, `onforward`, `onescape`), then runs listeners, then bubbles to `Parent` while `e.Propagate` is true
  ([Panel.Event.cs:214-290](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Event.cs#L214-L290)).
  Listeners come from Razor attributes, `AddEventListener`, or `[PanelEvent]` methods discovered by reflection (a `bool false` return stops propagation)
  ([Panel.Event.cs:25-120](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Event.cs#L25-L120)).
  Events can be queued with a time (`PendingEvents`) or dispatched immediately. The docs' public view: panels get no mouse input unless `pointer-events: all`; `e.StopPropagation()`; tooltips via `Tooltip`/`OnTooltip`
  ([UI Interactions](https://sbox.game/dev/doc/ui/interactions.md)).
- Keyboard/focus: focus changes are requested (`SetFocus`) and land on the next tick; click focus searches outward from the pressed panel for one with `AcceptsFocus && FocusOnClick`, following `FocusOwner` so popups keep their opener; Tab/Shift+Tab traverse
  ([UISystem.Focus.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.Focus.cs),
  [UISystem.FocusTraversal.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.FocusTraversal.cs)).
  Button events reach the focused panel through `InputEventQueue.TickFocused`, and `UISystem.TickInput` ends by telling the engine's input context whether mouse and buttons belong to the UI or the game (`Game.InputContext.UpdateInputFromUI`), based on whether any visible root wants the mouse and the focused panel's `ButtonInput`
  ([UISystem.cs:270-390](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.cs#L270-L390)).
- IME composition, clipboard (`NativeEngine.Sdl.SetClipboardText`), text selection and drag/drop events are part of the same model
  ([Input directory](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/UI/Input)).

### 3.3 Lifetime, binding and rebuild policy

- Deletion: `Delete()` adds `:outro` and defers; `Delete(true)` is immediate. `UISystem.RunDeferredDeletion` deletes panels whose transitions have finished, and `ClearRenderTree`/`DestroyUnseen` route Razor-owned panels through the same path with an "outro parent" so content stays alive during the parent's outro
  ([Panel.Delete.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Delete.cs),
  [UISystem.cs `RunDeferredDeletion`](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.cs)).
- Parameters: setting a parameter marks `parametersChanged`; the next tick runs `OnParametersSetAsync` then `OnParametersSet` and calls `StateHasChanged()`
  ([Panel.Property.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Property.cs)).
- When the engine decides to rebuild a Razor tree (all in `Panel.TickInternal` / `InternalTreeBinds`): `BuildHash()` differs from last frame; a `:bind` detected a change on either side; `StateHasChanged()` was called (also by `PanelComponent.StateHasChanged`, by async `@onclick` handlers after they complete, and by `OnParametersSet`); a `RenderFragment` parameter was re-assigned (`OnRenderFragmentChanged` bubbles up); a non-move mouse event reached the panel; or a hotload changed the markup checksum. Style changes alone never rebuild the tree; they re-run the cascade and layout.
- There is no `MarkRenderDirty`: "Drawing is regenerated on every command-list build, so there is no dirty state to mark"
  ([Panel.Render.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Render.cs)).

## 4. Rendering: what sits under the panels

### 4.1 History

Facepunch's own account: December 2020, "I ripped Panorana out and replaced it with a homebrew solution written in
c#", which "forc[ed] us to bind a bunch of stuff to render and get/control input"
([December 2020 news](https://sbox.game/news/dec2020/replacing-panorama)). March 2021: "The CSSBox container was
almost entirely rewritten, both borders and the box itself has a very nice, smooth subpixel analytical AA", 9-slice
border images, crisper text and emoji, and a goal of W3C conformance "with the advantage we are not carrying an
entire browser stack on our back" ([March 2021 news, "CSS Rendering Improvements"](https://sbox.game/news/march2021/css-rendering-improvements)).
In the public history, GPU text rendering landed on 2026-09-11
([`b020c8ef`](https://github.com/Facepunch/sbox-public/commit/b020c8ef), "GPU Text Rendering (#5685)") and the
`Painter` on 2026-09-13 ([`a73f077e`](https://github.com/Facepunch/sbox-public/commit/a73f077e), "Added Painter
(#5785)"); a May 2026 update refers to "moving to our UI batched renderer" and restores custom per-panel command
recording through `IPanelDraw` ([Update 26.05.20, "Panel Commandlist"](https://sbox.game/news/update-26-05-20)).

### 4.2 From panel tree to command list

`RootPanel.BuildCommandList` resets the root's `CommandList`, opens a `Painter` on it with the root bounds (world
panels pass a panel-to-world matrix as the viewport), and calls `Render` on the tree, then fixed-position overlays
([RootPanel.Render.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/RootPanel.Render.cs)).
`Panel.Render` is the per-panel recipe
([Panel.Paint.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Paint.cs)):

1. Skip if no computed style or invisible; enter a destination scope carrying inherited opacity, blend mode and the panel's transform matrix.
2. If the panel needs a layer (`filter`, `mask-image`, `filter: drop-shadow`, border-wrap), push an offscreen render target named `PanelLayer.{hash}` (content-hash cached for normally rendered roots)
   ([Panel.Layer.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Layer.cs)).
3. `FilterBackdrop` for `backdrop-filter`, outset shadows (for roots/layers), background + border as one `BoxDescriptor` (`painter.Rect(in descriptor)`), content (`OnDraw(Painter)` in panel-local coordinates, inline text, then `IPanelDraw.Draw(CommandList)`), inset shadows, outline.
4. Children, sorted by `order`/`z-index`, inside a destination clip with the inner corner radii (`overflow: hidden/scroll`), sibling outset shadows first, then scrollbars.
5. Pop the layer and composite it with filter, mask, drop shadows and border wrap (`Material.UI.Filter`, `DropShadow`, `BorderWrap` quads)
   ([Painter.Composite.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/Painter.Composite.cs)).

`UISystem.BuildCommandLists` does this for every active root (world panels also snapshot their world matrix into a
`ScenePanel` command list), then `CombineCommandLists` inserts each screen root's list into one `GlobalCommandList`
(roots that are `RenderedManually` or world panels are excluded)
([UISystem.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/UISystem.cs)).

### 4.3 The batcher and the shader

`PainterBatcher` owns per-frame `GpuTable`s (three frame slots, only new entries uploaded): `PainterTextInstances`,
`BoxInstances`, `ScissorBuffer` (a linked chain of rounded-rect clips, each with its own transform), `TransformBuffer`,
`GradientBuffer`, `BorderShapeBuffer`, `PathBuffer`, `PathNodeBuffer` and, when needed, `PolygonPointBuffer`. A draw
binds all tables as attributes, binds the glyph cache, sets `InstanceOffset`, blend mode, world-panel and
screen-pixel combos, and calls `commands.DrawIndexedInstanced( quadIndexBuffer, Material.UI.BatchedBox, count )`:
one six-index quad instanced `count` times
([PainterBatcher.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/PainterBatcher.cs),
[PainterBatcher.Commands.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/PainterBatcher.Commands.cs)).
Batches break on blend-mode changes, backdrop captures, layer targets and native commands.

The shader is `ui_cssbox_batched.shader`. Its `BoxInstanceData` is one record per box: rect, colour, horizontal and
vertical corner radii, four border widths and colours, a bindless background texture index and sampler, background
rect/tint/angle, border-image index/slice/tint, packed flags (mode, border-image mode, border style, repeat, clip),
scissor index, transform index, inverse scissor, text-mask index and a `ShapeIndex` into `BorderShapeBuffer` "or -1
for a plain rounded rect". Shape kinds: polygon, circle, polygon path, stroke path, capsule, crescent, heart,
simple line, polygon stroke; path primitives: segment, disc, join, arc, round join; caps: butt, square, round,
triangle, arrow. The vertex shader expands each instance to a quad "grown by a pixel so the outer half of the edge
antialiasing has somewhere to land", applies the instance transform, a layer matrix and, for world panels, a world
matrix ([ui_cssbox_batched.shader](https://github.com/Facepunch/sbox-public/blob/2643ebd6/game/core/shaders/ui_cssbox_batched.shader)).
Supporting includes: `ui/rounded_rect.hlsl`, `ui/border_style.hlsl`, `ui/shape_path.hlsl`, `ui/path_quad.hlsl`,
`ui/disc_coverage.hlsl`, `ui/batched_scissor.hlsl`, `ui/blur.hlsl`, `ui/gamma.hlsl`, `ui/lighting.hlsl`, `ui/text.hlsl`
([game/core/shaders/ui](https://github.com/Facepunch/sbox-public/tree/2643ebd6/game/core/shaders/ui)). Other UI
shaders: `ui_backdropfilter`, `ui_dropshadow`, `ui_filter`, `ui_borderwrap`, `ui_text`, `ui_basic`, `ui_cssbox`,
`text_gpufont_cs` ([game/core/shaders](https://github.com/Facepunch/sbox-public/tree/2643ebd6/game/core/shaders)).

How each visual feature is drawn, from the sources above:

- Rounded corners, borders (per-side widths/colours/styles), elliptical radii: analytic in the pixel shader from `BoxInstanceData`.
- Gradients: "evaluated in the pixel shader rather than baked to a texture" via `GradientBuffer`
  ([Styles.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Styles/Styles.cs)).
- Images and 9-slice border images: bindless texture indices in the instance; `Image`/`SvgPanel`/`ScenePanel` draw through `Panel.DrawTexture(Painter, ...)` with `object-fit`, repeat and rounded clipping.
- Box shadows: `ShadowDescriptor` instances (`RectShadow`) with blur/spread/inset, rendered by the same batch.
- Clipping: a scissor chain in `ScissorBuffer`, so nested `overflow: hidden` with rounded corners and transforms is resolved per pixel, not by hardware scissor.
- Filters/masks/opacity groups: `BeginLayer` allocates an RGBA8 render target (16 mips when blurring, mips generated with `GaussianBorder` downsampling) and composites it with `Material.UI.Filter`
  ([Painter.Layer.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/Painter.Layer.cs)).
- Backdrop blur: a frame grab of what has been drawn so far behind the rect, drawn with the backdrop shader; captures can be shared between panels whose intervening draws are independent (convars `ui_painter_share_backdrops`, `ui_painter_defer_batches`)
  ([PainterBatcher.Backdrops.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/PainterBatcher.Backdrops.cs)).
- Text: fonts are loaded from `/fonts/` as SkiaSharp `SKTypeface`s and matched by family/weight/italic
  ([FontManager.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/TextRendering/FontManager.cs));
  shaping, bidi, line breaking and layout use a vendored `Topten.RichTextKit`
  ([ThirdParty/Topten.RichTextKit](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/ThirdParty/Topten.RichTextKit),
  [TextBlock.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Engine/TextBlock.cs));
  glyph outlines are extracted once per process as quadratic Béziers with "Lengyel's bands" into three GPU buffers read by `ui/text.hlsl`
  ([GpuFontGlyphCache.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/TextRendering/GpuFont/GpuFontGlyphCache.cs));
  each laid-out run becomes box instances in modes 4/5 ("draw text straight from the glyph outlines ... text draws live every frame")
  ([GpuFontText.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/TextRendering/GpuFont/GpuFontText.cs),
  [PainterBatcher.Text.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/PainterBatcher.Text.cs)).
  COLR v0 colour-emoji layers are read from the font tables. `background-clip: text` renders the label to a texture used as a mask. `TextRendering.GetOrCreateTexture` still offers cached text textures for consumers that need one.
- Vector shapes from the Painter (polygons, strokes with joins/caps/dashes, Béziers, arcs, symbols) are path primitives in `PathBuffer`/`PathNodeBuffer` evaluated by `ui/shape_path.hlsl`; `Line` and `Polygon` have no vertex limit
  ([Painter.Path.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/Painter.Path.cs),
  [Painter Shapes](https://sbox.game/dev/doc/ui/painter/shapes.md)).

### 4.4 Public drawing APIs and how they relate

- `Painter` (public `ref struct`, `Sandbox.Painter`): state (`Fill`, `Stroke`, `TextStyle`, `Opacity`, `BlendMode`, `Transform`, `Clip`, `Scope()`), shapes (`Rect`, `Circle`, `Ring`, `Arc`, `Pie`, `Polygon`, `Line`, `LineSmooth`, `Bezier`, `Star`, `Cross`, `Tick`, `Arrow`, `Outline`, `RectShadow`, `Texture`, `Text`, `MeasureText`), `BeginLayer` with `Filter`/`Mask`, `FilterBackdrop`, and 3D `Line3D`/`Billboard` on camera painters
  ([Painter.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/Painter.cs),
  [Painter.Shapes.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/Painter.Shapes.cs),
  [Painter](https://sbox.game/dev/doc/ui/painter.md), [Fill & Stroke](https://sbox.game/dev/doc/ui/painter/fill-and-stroke.md),
  [Layers & Filters](https://sbox.game/dev/doc/ui/painter/layers.md), [Destinations](https://sbox.game/dev/doc/ui/painter/destinations.md)).
  Destinations: a panel's `OnDraw(Painter)` (owned by the panel, drawn after background and before children), `camera.BeginHud()` (after post-processing, beneath UI) and `camera.BeginOverlay()` (above everything), `Painter.Begin(texture)` (persistent pixels, `GetBitmap()` read-back), `Painter.Begin(commandList[, bounds])` at any render stage; `Begin*` painters submit on `Dispose`.
- The Painter is not a separate renderer: it is the same `PainterBatcher`/`CommandList` path the CSS boxes use, so a panel's custom drawing is batched with its CSS ("These draw calls will be batched together with the panel's CSS-styled content")
  ([Panel.Draw.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Panel/Panel.Draw.cs)).
- Obsolete wrappers: `HudPainter` (`Camera.Hud`/`Camera.Overlay`) and the static `Panel.Draw.Rect/Text/Texture` both forward into the Painter (`BeginLegacy`, `LegacyRect`, ...)
  ([HudPainter.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/HudPainter.cs),
  [Painter.Compatibility.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting/Painter.Compatibility.cs),
  [HudPainter migration](https://sbox.game/dev/doc/ui/hudpainter.md)).
- `IPanelDraw.Draw(CommandList)` lets a panel append arbitrary GPU commands (custom materials, `DrawQuad`) in object space between the CSS passes
  ([IPanelDraw.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Render/IPanelDraw.cs),
  [Update 26.05.20](https://sbox.game/news/update-26-05-20)).
- Lower still, `Graphics.Draw(Span<Vertex>, material, ...)` is the immediate vertex path ("our entry point into the engine for all draws"), which calls native `RenderTools.SetRenderState` and `RenderTools.Draw`
  ([Graphics.Draw.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Graphics.Draw.cs),
  [RenderTools.def](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Definitions/engine/Render/RenderTools.def)).
- `Gizmo.Draw` is the editor/gizmo drawing API (used, for example, by the `WorldPanel` component to draw its rectangle in the editor); its implementation was not examined for this note.

### 4.5 Where the UI is executed

The combined list is executed on the render thread by a native-called hook: `Graphics.Hooks.RenderUiOverlay` runs
`GlobalContext.Current.UISystem.Render()`, which executes `GlobalCommandList`
([Graphics.Hooks.cs:52-60](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Graphics.Hooks.cs#L52-L60)).
`ScreenPanel` roots are rendered per camera at the stage their `Timing` selects (`OnCameraRenderUI`, iterating a
snapshot `Scene.renderScreenPanels`), and `BeforePostProcess` roots set an `enableEarlyUI` camera attribute
([CameraComponent.cs:575-620](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Scene/Components/Camera/CameraComponent.cs#L575-L620)).
World panels execute inside `ScenePanelObject.RenderSceneObject` as translucent scene objects
([ScenePanelObject.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/WorldPanel/ScenePanelObject.cs)).
`RootPanel.RenderManual(opacity)` exists for code that wants to execute a root's list inside its own render block.

## 5. C# versus native, and the boundary

### 5.1 What is where

Per the README, the repository "contains the C# engine, editor, tooling and game content. The native Source 2 core is
distributed as prebuilt binaries" ([README.md](https://github.com/Facepunch/sbox-public/blob/2643ebd6/README.md)),
and "the managed side calls into them through a generated interop layer" produced from `engine/Definitions/*.def`
([AGENTS.md](https://github.com/Facepunch/sbox-public/blob/2643ebd6/AGENTS.md),
[engine/Definitions](https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Definitions)). Everything in
sections 1-4 (Razor compilation, type library, stylesheet parsing, cascade, layout, text shaping, batching, the
command-list recorder) is in C# under `engine/Sandbox.Engine`, `engine/Sandbox.Layout`, `engine/Sandbox.Razor`,
`engine/Sandbox.Compiling` and `engine/ThirdParty`. Native pieces the UI depends on: the render device and context,
materials and the Source 2 shader pipeline (shaders are content in `game/core/shaders`, written in Valve's VFX
format, see [December 2020 news, "Shaders"](https://sbox.game/news/dec2020/replacing-panorama)), SDL for window
input and clipboard, and SkiaSharp's native library for font parsing and outlines.

A search of the managed tree and the interop definitions for "Panorama" finds only a Material Design icon name
(`panorama_horizontal`) on the `WorldPanel` component; the definitions contain no UI, panel, font or text interfaces
(the only panel-related natives are `SourceEnginePanelAppInit`/`SourceEnginePanelAppFrame` in
[engine.globals.def](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Definitions/engine/engine.globals.def),
whose semantics are not documented in the repo; the name suggests the app-frame entry for panel windows, which is
unverified). Whether any Panorama code still exists inside the prebuilt native binaries cannot be determined from
public sources.

### 5.2 What C# hands to native per frame

- GPU buffers: `GpuBuffer<T>` wraps `g_pRenderDevice.CreateGPUBuffer` and uploads with `SetData` (semistatic); the batcher's tables and the glyph cache are such buffers
  ([GpuBuffer.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/GpuBuffer.cs)).
- Render attributes (named constants, buffers, bindless texture indices, combos) and a material: `Graphics.DrawIndexedInstanced` does `SetRenderState(material, attributes)`, `Context.BindIndexBuffer`, then native `IRenderContext.DrawIndexedInstanced`
  ([Graphics.Draw.Buffer.cs:189-200](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/Graphics.Draw.Buffer.cs#L189-L200),
  [IRenderContext.def](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Definitions/common/Render/IRenderContext.def)).
- Render targets for layers and backdrop grabs (`CommandList.GetRenderTarget`, `PushRenderTarget`, `GenerateMipMaps`, grab textures).
- Nothing else: the `CommandList` itself is managed (a list of `Entry` structs with function pointers, executed by `ExecuteEntries` under a lock on the render thread)
  ([CommandList.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/CommandList/CommandList.cs),
  [CommandList.Resources.cs:78-104](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/Render/CommandList/CommandList.Resources.cs#L78-L104)).
  Native calls back into managed for render stages (`Graphics.Hooks`), and the UI's own hook executes the combined list.

So the native-side contract the UI needs is small and generic: create/upload structured and index buffers, bind a
material with attributes (including bindless textures), draw indexed-instanced, create/bind render targets with
mips, and read back the current colour target for backdrop effects. Scissoring, transforms, text and shapes are all
data in buffers interpreted by the shader.

### 5.3 What this implies when C# is optional

In s&box, C# is never optional: the editor, menu, dev tools and games are all managed, and the UI has no native
fallback. The parts of the UI design that assume a managed runtime are: the Razor compiler and Roslyn running
in-process; the `TypeLibrary` reflection that turns tag names into types and attribute names into properties
([RenderTree.Block.cs `FindOrCreateElement`](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Razor/RenderTree.Block.cs));
`[PanelEvent]` reflection; hotload; the `Styles` class generated from a T4 template; the layout engine; RichTextKit
text layout; and the batcher. The parts that are language-neutral are the shader contract (instance tables, scissor
chain, glyph buffers) and the command-list execution model.

## 6. Lineage: Panorama

Valve describes Panorama as "a new Valve-developed UI framework ... heavily influenced by and closely resembles modern
web authoring (HTML5/CSS/JS)" that "completely replaced Scaleform"; its concepts are Panels ("basically an HTML
element"), `.xml` layout files, `.css` style sheets, `.js` scripts and a panel event system, with "Some parts of CSS
... tweaked to make them simpler" and "Some web features ... simply missing"
([Panorama Overview](https://developer.valvesoftware.com/wiki/Panorama/Overview), the redirect target of the
Dota 2 Workshop Tools Panorama page). Its layout "is a simplified version of HTML layout": `flow-children:
down|right|right-wrap|down-wrap`, `horizontal-align`/`vertical-align`, direct `x`/`y`/`z` positioning, the CSS box
model, and automatic scaling "based on the ratio of the current vertical resolution compared to 1080 pixels" with
aspect-ratio classes on the root ([Panorama Layout](https://developer.valvesoftware.com/wiki/Panorama/Overview/Layout)).
Its property list (dumped 2025-12-06) includes web-like properties plus custom ones such as `background-blur`, `blur`,
`brightness`/`contrast`/`saturation`/`hue-rotation`, `wash-color`, `opacity-mask`, `sound`/`sound-out`, `ui-scale`,
`texture-sampling`, `transition-*` and `animation-*`, and pseudo-selectors `:hover`, `:focus`, `:active`, `:disabled`,
`:enabled`, `:inspect`, `:selected`, `:descendantfocus`, `:parentdisabled`, `:layoutloading`, `:layoutfailed`,
`:activationdisabled` ([Panorama CSS Properties](https://developer.valvesoftware.com/wiki/Panorama/Overview/CSS_Properties)).
Content files are hot-reloaded while the tools run, and a Panorama Debugger inspects the live tree
([Panorama Overview](https://developer.valvesoftware.com/wiki/Panorama/Overview)).

What s&box did with it, in Facepunch's own words ([December 2020 news](https://sbox.game/news/dec2020/replacing-panorama)):
Panorama was used first (the post refers back to an earlier post praising it; that post was not located), then
"ripped out and replaced ... with a homebrew solution written in c#" because "It's easier and better to rewrite it in
c# than to fix the stuff that annoys me in c++"; layout moved to Yoga flexbox because Panorama's "non standard layout
system ... is kind of flexxy, but it's not as powerful/complete as proper flexboxing"; "The CSS transitions in
Panorama are wired backwards"; "Things like pointer-events and cursor work"; the goal was "to make everything more in
line with how you'd expect real stylesheets to behave. We can add some special magic to the sheets, like sounds and
different rendering modes".

Kept, replaced, dropped (comparing the two sources above with sections 1-4):

- Kept: the panel-tree-plus-stylesheet model; a web-flavoured CSS dialect with game extras (Panorama `sound`/`sound-out` -> s&box `sound-in`/`sound-out`; Panorama `blur`/`background-blur`/`brightness`/`saturation`/`hue-rotation` -> s&box `filter`/`backdrop-filter`; `opacity-mask` -> `mask-image`; `border-image` 9-slice in both); 1080p-height-relative scaling as the default; `:hover`/`:focus`/`:active`/`:disabled`/`:enabled` and a descendant-focus state (`:descendantfocus` -> `:focus-within`); live reload of style files; a tree inspector (the editor's Explorer lists panels "like html tags" with their defining Razor file and line, [October 2022 news](https://sbox.game/news/october2022/explorer), backed by `Panel.SourceFile`/`SourceLine`).
- Replaced: XML layouts -> Razor markup compiled to C#, or C# directly; JavaScript -> C#; Panorama events -> C# virtuals, listeners and `[PanelEvent]`; `flow-children`/`horizontal-align` -> flexbox (Yoga, then `Sandbox.Layout` with block/grid/inline); Panorama's C++ renderer -> the C# batcher and `ui_cssbox_batched` shader.
- Dropped: Panorama itself (no symbol survives in the managed tree or interop definitions); its non-standard transition semantics; `:selected`, `:parentdisabled`, `:layoutloading`, `:layoutfailed`, `:activationdisabled`, `ui-scale-*`, `wash-color` (s&box has `background-tint`/`filter: tint()` instead), and the aspect-ratio root classes (not found in s&box sources or docs).
- Added beyond Panorama: `:intro`/`:outro` lifecycle pseudo-classes, `border-shape`, `text-stroke`, `background-clip: text`, CSS grid/block/inline, `@mixin`/`$variables`, two-way binds, and the Painter API.

## Implications for FenrirEngine

This section only states what the s&box design tells us, measured against what Fenrir already has
([packages/FenrirDraw2D/README.md](../../../packages/FenrirDraw2D/README.md),
[packages/FenrirScripting/README.md](../../../packages/FenrirScripting/README.md)).

- Parts of s&box's UI that assume C# is always present: the authoring front-end (Razor -> C# via an in-process Razor compiler and Roslyn), tag/attribute resolution by reflection (`TypeLibrary`), `[PanelEvent]` discovery, hotload of panel code and markup, the style model (a generated C# `Styles` class, a C# SCSS parser and cascade), the layout engine (`Sandbox.Layout`, pure C#), text layout (RichTextKit, C#) on top of SkiaSharp, the `Painter`/`PainterBatcher` and the managed `CommandList`. In s&box there is no UI without the managed runtime; the editor, menu and dev tools are panels too, and editor panels run in OS windows through `UISurface`/`PanelWindows`
  ([PanelWindows.cs](https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Systems/UI/Surface/PanelWindows.cs)).
- Parts that are language-neutral: the data the renderer consumes. A frame of s&box UI is a set of structured buffers (`BoxInstances`, glyph instances, scissor chain, transforms, gradients, shapes, path primitives and nodes), a handful of render-target operations (layers, mip generation, backdrop grabs), and one instanced quad draw per batch with a fixed shader family. None of that cares which language filled the buffers.
- The minimum native-side contract s&box needed from Source 2 for its UI: create and update GPU buffers; bind a material plus named attributes, including bindless texture indices and combos; `DrawIndexedInstanced`; push/pop render targets with mip generation; grab the current colour target; and callbacks at render stages. Scissoring is not a native feature in this path (it is per-pixel in the shader against the scissor chain), and neither is text (glyph outlines in buffers).
- How that compares with Fenrir today, factually: `DrawList2D` is a vertex/index batch with a pipeline table (shader asset, vertex layout, index type, blend mode, colour spaces), per-command texture slots (max 4), an optional hardware scissor rect, and up to 128 bytes of parameters, drawn by FenrirRenderer's overlay. s&box's batched path instead keeps geometry as a single shared quad and puts everything else in instance tables bound as shader resources; its vertex stream is empty apart from `SV_VertexID`/`SV_InstanceID`. Its older `ui_basic`/`ui_text` shaders and the `Graphics.Draw(Span<Vertex>)` immediate path are closer to Fenrir's vertex-batch model; the batched box path needs structured-buffer binding, bindless (or indexed) textures and render-target control that `DrawList2D` does not describe. Fenrir's scripting contract (`Fenrir.Scripting` over a native host, bindings generated from TOML descriptions, package extensions deployed beside the executable) is the opposite direction from s&box: s&box's native core exposes rendering primitives to C#, which owns the UI; Fenrir's native core owns the renderer and optionally hosts C#.
- The s&box facts that bear most on an engine where C# is optional: (1) everything above the GPU contract is replaceable per language only if the GPU contract is the seam, which in s&box it is; (2) the retained tree, cascade and layout are the bulk of the C# code and are what Panorama used to do natively before Facepunch moved them up; (3) hot reload of styles is a filesystem watcher plus "dirty all styles", independent of code hotload; (4) Yoga's flexbox was adequate for five years of s&box UI before block/grid/inline were added, and the replacement remained a port of Yoga's algorithm with conformance fixtures from Yoga and Taffy.

## Not verified or not found

- The earlier Facepunch post praising Panorama (referenced by the December 2020 post) was not located.
- The internal PR numbers in commit messages (#5711, #5685, #5785) do not resolve on the public repository; dates and SHAs are from the public history, which starts at the 2025-11-24 "Open source release" commit, so the pre-2025 evolution of the renderer is known only from the news posts cited.
- `wiki.facepunch.com/sbox/*` pages returned 404 during this research and were not used; `docs.facepunch.com` is behind Cloudflare Access.
- The native binaries were not inspected; whether Panorama code still exists inside them is unknown. `SourceEnginePanelAppInit` is the only "panel" named native entry point and is undocumented in the repo.
- `Gizmo.Draw` internals, the editor's Qt-based tools (`engine/Definitions/tools/Qt*.def` show Qt widgets are bound for tooling), and the `Sandbox.Menu` project were not examined.
- The sbox.game API reference pages are JavaScript-rendered and were not read; API facts come from the source tree at the pinned commit.

## Sources

Engine source (all at commit `2643ebd62bf7947129fa4f9d8f490d7580be7903`, 2026-11-10):

- https://github.com/Facepunch/sbox-public/blob/2643ebd6/README.md and `AGENTS.md`
- https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Compiling/Compiler/Compiler.Razor.cs
- https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Razor/Razor.cs and `engine/Sandbox.Razor/Microsoft.AspNetCore.Razor.Language/` (ComponentRuntimeNodeWriter.cs, ComponentDocumentClassifierPass.cs, ComponentMarkupBlockPass.cs, MarkupElementIntermediateNode.cs, CodeGeneration/CodeWriterExtensions.cs)
- https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Microsoft.AspNetCore.Components/RenderTreeBuilder.cs
- https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Generator/RazorGenerator.cs
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/UI (Panel/*.cs, Razor/*.cs, Styles/*.cs, Parser/*.cs, Animation/*.cs, Layout/PanelLayout.cs, Input/*.cs, Controls/*.cs, Surface/*.cs, WorldPanel/*.cs, RootPanel*.cs, UISystem*.cs, Render/IPanelDraw.cs)
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Scene/Components/UI (PanelComponent.cs, ScreenPanel.cs, WorldPanel.cs)
- https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Sandbox.Engine/Scene/Components/Camera/CameraComponent.cs
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Layout (Sandbox.Layout.csproj, LayoutNode.cs, Layout/Flex/FlexLayout.cs, Styles/Enums.cs) and https://github.com/Facepunch/sbox-public/blob/2643ebd6/engine/Tests/Sandbox.Test.Unit/Layout/Tools/README.md
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/Render/Painting (Painter*.cs, PainterBatcher*.cs, HudPainter.cs)
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/Render/TextRendering (FontManager.cs, GpuFont/*.cs, TextRendering.cs)
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Sandbox.Engine/Systems/Render/CommandList and `Graphics.Draw.cs`, `Graphics.Draw.Buffer.cs`, `Graphics.Hooks.cs`, `GpuBuffer.cs`
- https://github.com/Facepunch/sbox-public/tree/2643ebd6/engine/Definitions (engine/Render/RenderTools.def, common/Render/IRenderContext.def, engine/engine.globals.def)
- https://github.com/Facepunch/sbox-public/blob/2643ebd6/game/core/shaders/ui_cssbox_batched.shader and https://github.com/Facepunch/sbox-public/tree/2643ebd6/game/core/shaders/ui
- Commits: https://github.com/Facepunch/sbox-public/commit/71f26605 (2025-11-24, open source release), https://github.com/Facepunch/sbox-public/commit/a7b51476 (2026-09-05, Replace Yoga with Sandbox.Layout), https://github.com/Facepunch/sbox-public/commit/b020c8ef (2026-09-11, GPU Text Rendering), https://github.com/Facepunch/sbox-public/commit/a73f077e (2026-09-13, Added Painter)

Documentation (sbox.game/dev/doc, read as `.md`):

- https://sbox.game/dev/doc/ui.md
- https://sbox.game/dev/doc/ui/razor-panels.md and https://sbox.game/dev/doc/ui/razor-panels/razor-components.md
- https://sbox.game/dev/doc/ui/styling-panels.md, https://sbox.game/dev/doc/ui/styling-panels/style-properties.md, https://sbox.game/dev/doc/ui/styling-panels/border-shape.md
- https://sbox.game/dev/doc/ui/interactions.md
- https://sbox.game/dev/doc/ui/painter.md, .../painter/shapes.md, .../painter/fill-and-stroke.md, .../painter/text.md, .../painter/layers.md, .../painter/destinations.md, https://sbox.game/dev/doc/ui/hudpainter.md
- https://sbox.game/dev/doc/code/advanced-topics/hotloading.md
- https://sbox.game/llms.txt (index)

Facepunch news posts:

- https://sbox.game/news/dec2020/replacing-panorama (19 December 2020)
- https://sbox.game/news/march2021/css-rendering-improvements (31 March 2021)
- https://sbox.game/news/october2022/explorer (October 2022)
- https://sbox.game/news/update-26-05-20 (20 May 2026)

Valve:

- https://developer.valvesoftware.com/wiki/Panorama/Overview (redirect target of Dota_2_Workshop_Tools/Panorama)
- https://developer.valvesoftware.com/wiki/Panorama/Overview/Layout
- https://developer.valvesoftware.com/wiki/Panorama/Overview/CSS_Properties
- https://developer.valvesoftware.com/wiki/CS2/Panorama (empty page at time of research)

FenrirEngine context:

- `packages/FenrirDraw2D/README.md`, `packages/FenrirScripting/README.md` (this repository)
