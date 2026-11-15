# 01: Scoped stylesheets and stylesheets from text

**What to build:** a stylesheet loaded with a scope class styles only elements carrying that class, `::deep` reaches past it, and C# can load scoped sheets from a path or from text (spec: "FenrirUI: scoped stylesheets"; ADR-0070, amending ADR-0064).

- `Stylesheet::Parse`, `UIRoot::LoadStylesheet(path, scope)` and `UIRoot::AddStylesheet(text, name, scope)` take an optional scope. An empty scope means unscoped, as today. After nesting is resolved, the scope class is appended to every rule's rightmost compound selector.
- `::deep` in a scoped sheet moves the scope to the compound before it and leaves a descendant combinator. In an unscoped sheet it is an error for that rule, logged with the file and line.
- `@import`ed sheets inherit the scope. `@keyframes` and `@font-face` are not scoped. A sheet added again under the same path or name and scope is not added twice. Hot reload re-reads a scoped file with its scope.
- Confirm that `SetStyleProperty(element, name, "")` resets an inline declaration to "not set", or add that, since the builder resets dropped declarations that way (spec: "The builder").
- The C# binding: `LoadStylesheet` gains the scope, and `AddStylesheet(root, text, name, scope)` is new, both generated from the C++ natives as ticket 20's are.
- FenrirUI's README: scoped sheets and `::deep` in the stylesheet section.

**Blocked by:** none

**Touches:** `packages/FenrirUI/include/`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/Fenrir.UI/Generated/`, `packages/FenrirUI/README.md`

**Status:** ready-for-agent

- [ ] C++ tests: `.a .b` scoped to `s-x` matches `.b.s-x` under `.a` and not a `.b` without the class; `.a > label` and nested rules; `.list ::deep label` matches a label without the class under a scoped `.list`; `::deep` in an unscoped sheet is a logged error with its line; `@import` under a scope; the same sheet added twice with one scope is one sheet; hot reload of a scoped file keeps the scope.
- [ ] A scripting test loads a scoped sheet from text and from a path and reads the resolved style of a scoped and an unscoped element.
- [ ] An inline declaration set to `""` falls back to the stylesheet's value.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py`, clang-format and the binding regeneration check pass.
