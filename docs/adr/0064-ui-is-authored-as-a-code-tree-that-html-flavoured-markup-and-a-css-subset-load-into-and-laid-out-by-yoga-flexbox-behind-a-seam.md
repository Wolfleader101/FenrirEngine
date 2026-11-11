---
status: proposed
---

# UI is authored as a code tree that HTML-flavoured markup and a CSS subset load into, and laid out by Yoga flexbox behind a seam

The UI is for a modern software developer, who the research (`.scratch/fenrir-ui/research/engine-ui-authoring-models.md`) finds expects the same things in every system that states approachability as a goal: a text format for structure, a stylesheet with selectors, classes, variables and transitions, a flexbox or Measure/Arrange layout in pixel units with a scale factor, hot reload, and a code path that needs none of the files. Unity (UXML/USS/Yoga), RmlUi (RML/RCSS), Gameface (HTML/CSS/Yoga) and s&box (Razor/SCSS/Yoga then its own port) made those choices; Noesis chose XAML; Unreal and Godot chose editors plus code. This ADR picks the authoring model, the stylesheet dialect and the layout engine for FenrirUI. Settled in a grilling session on 2026-11-10.

## The code tree is the base

Every element can be made, parented, classed and styled from C++ and from C# (ADR-0061) with no file, and that API is complete: markup and stylesheets are conveniences that load into the same tree. A C++ custom element subclasses `Element` with measure and paint virtuals and registers its tag name; a C# one composes built-ins and paints from a hosted stage.

## Markup is HTML-flavoured structure with no logic

A **markup document** is a UTF-8 XML-like file of element tags (`<panel class="hud">`, `<label text="...">`, `<button>`, and any registered custom tag), with `id`, `class`, an inline `style` attribute, each control's own attributes, `<include src>` for reuse, and `<style src>` or an inline `<style>` block to attach stylesheets. It holds no expressions, loops or bindings: data reaches elements by push (ADR-0065). XAML was the other candidate; it buys WPF literacy and costs templates, triggers and a far larger grammar.

## Stylesheets are a CSS subset with CSS variables and nesting

A **stylesheet** is CSS syntax: type, class, id, descendant and child selectors, pseudo-classes (`:hover`, `:active`, `:focus`, `:focus-within`, `:disabled`, `:checked`, `:first-child`, `:last-child`, `:intro`, `:outro`), **CSS custom properties** (`--x`, `var()` with a fallback) rather than SCSS `$` variables, **CSS nesting** (`&:hover`, `> .child`) rather than mixins, `@import`, `@keyframes`, `transition-*` and `animation-*`, and `@font-face` to name a font. The property set is the box and flex properties Yoga lays out plus backgrounds, borders, radii, shadows, opacity, transforms, overflow, text and the filters that offscreen groups draw (ADR-0060); the spec lists it. s&box's SCSS dialect was the model and was deliberately not copied: custom properties and nesting are now standard CSS, so what a web developer knows transfers without a dialect to learn.

## Layout is flexbox, Yoga, behind a seam

**Yoga** (C++, MIT) is a submodule and the only layout engine, as it is for Unity, Gameface and React Native, and was for s&box for five years. Its semantics are flexbox with absolute positioning, percentages and gaps; block flow, grid and inline text flow are out of scope. **Yoga is used only inside FenrirUI's layout bridge**: computed style in, a measure function per leaf (text, image), rectangles out, and no Yoga type in a public header. The bridge is the one file a replacement rewrites, which is how s&box swapped Yoga for its own port without touching a panel; Yoga's conformance fixtures make a replacement testable. **A grid container, when a game asks for one, is built in FenrirUI** as a container that measures itself to Yoga and places its own children, so grid arrives without replacing Yoga.

Units are logical pixels, the units of `DrawList2D` and the mouse, and percent. Each UI root has a **scale rule**: by default 1080 UI units span the target's pixel height (Panorama's and s&box's rule, so a layout authored at 1080p keeps its proportions at every resolution), or UI units equal the target's logical units, which follow the operating system's content scale. Laid-out rectangles are snapped to whole pixels so borders and text stay crisp.

## Considered

- **XAML (Noesis).** WPF's model is complete and designer-tooled, but its grammar (templates, triggers, resource dictionaries, dependency properties) is the reason Noesis says any deviation from WPF is a bug: it has to be all of it or none.
- **s&box's SCSS dialect.** `$variables`, `@mixin` and `@include` are what the web had before custom properties and nesting became CSS; a web developer today learns the standard forms first.
- **A Rust solver (Taffy) for flex and grid.** FFI into a C++ engine for one library, when Yoga is C++ with a C API and grid can be built above it.
- **Writing the flex solver.** Weeks of work and the usual source of layout bugs, to own what Yoga's fixtures already prove.
- **Editors only (Unreal, Godot).** The engine has no editor yet; text files and hot reload are what a developer has on day one.
