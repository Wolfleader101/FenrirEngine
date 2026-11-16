---
status: accepted
---

# Razor component stylesheets are scoped by a scope class the native stylesheet loader applies

s&box loads `Foo.razor.scss` beside `Foo.razor` as an ordinary sheet. Blazor scopes `Foo.razor.css`: the build gives every element of the component an attribute `b-<hash>` and rewrites each selector to require it, and `::deep` reaches into child components. FenrirUI's selectors have no attribute selectors (ADR-0064), and its stylesheet loader already parses selectors, resolves nesting and hot-reloads files. Settled in a grilling session on 2026-11-15.

## Decision

- **Every element a component's builder creates gets the component's scope class**, `s-` followed by a hash of the component type's full name, added once when the element is made. A child component's elements carry the child's scope, not the parent's.
- **FenrirUI's stylesheet loading takes an optional scope** (`LoadStylesheet(path, scope)`, `AddStylesheet(text, name, scope)`).
  - With a scope, the parser adds the scope class to the rightmost compound selector of every rule, after nesting is resolved.
  - `::deep` moves it to the compound before the combinator, so `.list ::deep label` styles labels in child components.
  - Hot reload re-reads a scoped file with its scope.
  - The feature is native and generic: a markup document's sheet could be scoped too. Nothing in it names Razor.
- **`Foo.razor.css` is found by convention.** FenrirUI's Razor generator, which `Fenrir.UI.Razor.props` adds beside Razor's, sees `.razor.css` files as additional files and records each one's path and text on its component type. The runtime loads it once per UI root, with `Foo`'s scope, when the first `Foo` mounts there. It loads by path when the file exists, so editing the sheet hot-reloads it, and from the recorded text otherwise.
- **A `<style>` block in a `.razor` file** is scoped the same way, through `AddStylesheet` from its text, once per root and component type.
- **A component's root element also gets a readable class**, its type name in kebab case (`inventory` for `Inventory`), so a game's own sheets can target it unscoped.

## Considered

- **Unscoped sheets found by convention** (s&box), with authors nesting under the root class by hand. Every author scopes by discipline, and two components' `.title` collide.
- **Rewriting selectors at build time** (Blazor's way, in an MSBuild task or generator). A second CSS parser in C#, and editing the `.css` alone would need a rebuild to take effect.
- **Rewriting in C# at load.** The same second parser.
- **Scope attributes instead of a class.** FenrirUI would need attribute selectors for this alone.

## Consequences

- *Amends ADR-0064:* stylesheets may be loaded with a scope, and `::deep` is part of the dialect when they are.
- A selector that names only an ancestor outside the component (`.hud .title`) still matches only the component's own `.title`, since the scope lands on the rightmost compound. That is Blazor's rule.
