# 08: Stylesheet parser and cascade

**What to build:** a CSS-subset stylesheet given as a string styles a root's elements by selector, with specificity, inheritance, custom properties and nesting (spec: "FenrirUI" stylesheet and property set; ADR-0064).

- **Parser**: rules with selector lists (type, `.class`, `#id`, descendant, `>` child, the pseudo-classes `:hover :active :focus :focus-within :disabled :checked :first-child :last-child :intro :outro :root`), declarations for the spec's property set with the shorthands (margin, padding, border, border-radius, background, flex, transition, animation), lengths in px and %, colours (hex, `rgb()`, `rgba()`, `hsl()`, named), `url()`, `linear-gradient()`, `radial-gradient()`, custom properties and `var()` with a fallback, nesting with `&` and bare child selectors, `@keyframes` and `@font-face` parsed and stored (used by tickets 10 and 16), `@import` recorded for ticket 16, comments. Malformed declarations are logged with line and skipped; a malformed rule skips the rule; unknown properties and pseudo-classes are logged.
- **Cascade**: rules bucketed by rightmost simple selector for matching; specificity and order resolve conflicts; inline style wins; inherited properties inherit (text properties, colour, cursor, pointer-events); custom properties resolve at use with inheritance; a changed class, id, pseudo-class, inline style or sheet marks the subtree dirty. The style cascade runs in the frame before layout.
- A root holds a stylesheet collection; `AddStylesheet(root, text, name)` and removal; the resolved style is readable per element for tests.

**Blocked by:** 07

**Touches:** `packages/FenrirUI/`

**Status:** ready-for-agent

- [ ] Parser tests: every selector form and pseudo-class; every shorthand expands as CSS does; each error kind reports the right line and leaves the rest of the sheet intact; nesting flattens to the expected selectors; `var()` with and without fallback; custom properties inherit.
- [ ] Cascade tests in a headless App: specificity order (type < class < id), later rule wins at equal specificity, inline wins over all, inheritance down three levels, a class change restyles only its subtree, a pseudo-class flag flip restyles the element.
- [ ] Layout test: a sheet-styled tree lays out as the same tree styled inline.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
