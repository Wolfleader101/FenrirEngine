# 07: Scoped component stylesheets

**What to build:** `Foo.razor.css` and `<style>` blocks style only `Foo`'s elements, `::deep` reaches its children, and editing the `.razor.css` hot-reloads it (spec: "Scoped component stylesheets", "`Fenrir.UI.Razor.props` and the generators"; ADR-0070).

- The scope class is `s-` followed by eight hex digits of FNV-1a over the type's full name. The builder adds it to every element it creates, generated labels and folded-markup elements included.
- **The generator.** `Fenrir.UI.Razor.Generators`, added with `.razor.css` `AdditionalFiles` by `Fenrir.UI.Razor.props`, emits a partial class carrying `[ComponentStylesheet(path, text)]` for each `.razor.css`. It names the class as Razor does (root namespace and folder, or an `@namespace` in the file or its `_Imports.razor`), and warns for a `.razor.css` with no matching `.razor`.
- **Loading.** On a type's first mount into a root, its sheet loads with the scope: by path when the file exists, so hot reload works, otherwise from the text. `<style>` blocks, folded or not, load once per root and type through `AddStylesheet` (ticket 04's sheet hook).

**Blocked by:** 01, 05

**Touches:** `packages/FenrirUI/Managed/Fenrir.UI.Razor/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Generators/`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.props`, `packages/FenrirUI/Managed/Fenrir.UI.Razor.Tests/`, `packages/FenrirUI/Scripting/Tests/`, `packages/FenrirUI/README.md`

Stacked on 06.

**Status:** ready-for-agent

- [ ] A test pins the scope class of a known type name.
- [ ] Scripting tests:
  - two components that both style `.title` each colour only their own;
  - a parent's `::deep label` reaches a child's label, and a plain `label` rule does not;
  - a `<style>` block scopes like a file;
  - a game sheet's rule on the readable class applies;
  - editing a `.razor.css` written to a temporary directory changes the resolved style on the next reload poll;
  - a component in a namespace set by `@namespace` finds its sheet.
- [ ] A second mount of the same type into the same root adds no second sheet.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
