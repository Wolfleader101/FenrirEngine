# 02: Native scoped stylesheets

**What to build:** a stylesheet loaded with a scope class styles only elements carrying that class, `::deep` reaches past it, and a sheet can be added from text with a scope. This is all C++, generic, and names nothing of Razor (spec: "FenrirUI: scoped stylesheets"; ADR-0070, amending ADR-0064).

- `Stylesheet::Parse`, `UIRoot::LoadStylesheet(path, scope)` and `UIRoot::AddStylesheet(text, name, scope)` take an optional scope. An empty scope means unscoped, as today. After nesting is resolved, the scope class is appended to every rule's rightmost compound selector.
- In a scoped sheet, `::deep` moves the scope to the compound before it and leaves a descendant combinator. In an unscoped sheet it is an error for that rule, logged with the file and line.
- `@import`ed sheets inherit the scope. `@keyframes` and `@font-face` are not scoped. A sheet added again under the same path or name and scope is not added twice. Hot reload re-reads a scoped file with its scope.
- Setting an inline declaration to `""` (`SetStyleProperty(element, name, "")`) resets it to "not set". Confirm it, or add it: the Razor builder resets a dropped declaration that way.
- FenrirUI's README: scoped sheets and `::deep` in the stylesheet section. The C# natives come with ticket 07, so this ticket stays off the scripting files.

**Blocked by:** none

**Touches:** `packages/FenrirUI/include/`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/README.md`

Stacked on 01 (the same native files). It runs beside 03, whose files are disjoint.

**Status:** ready-for-agent

- [ ] C++ tests:
  - `.a .b` scoped to `s-x` matches `.b.s-x` under `.a`, and not a `.b` without the class;
  - child combinators and nested rules are scoped;
  - `.list ::deep label` matches a label without the class under a scoped `.list`;
  - `::deep` in an unscoped sheet is a logged error with its line;
  - `@import` under a scope;
  - one sheet added twice with one scope is one sheet;
  - hot reload of a scoped file keeps the scope;
  - an inline declaration set to `""` falls back to the stylesheet's value.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
