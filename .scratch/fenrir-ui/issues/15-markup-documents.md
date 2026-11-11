# 15: Markup documents

**What to build:** a screen's structure written as an HTML-flavoured document loads into a root as elements, with errors that name the line (spec: "FenrirUI" markup document; ADR-0064).

- **Parser** of the package's own: tags for every registered element (built-in and custom, through the tag registry), `id`, `class`, inline `style`, control attributes as strings each control parses (`text`, `src`, `value`, `min`, `max`, `step`, `checked`, `disabled`, `placeholder`, `focusable`, and the rest the controls declare), bare text content assigned to the element's text when it has one and an error otherwise, `<include src>` splicing another document's elements with recursion detected, `<style src>` and inline `<style>` attaching sheets to the root (the sheet text goes to ticket 08's parser; file paths resolve in ticket 16, this ticket takes sheets from strings and a resolver callback), the five XML entities and numeric ones, comments; no namespaces, processing instructions, DTDs or CDATA.
- **Loading**: from a string this ticket (files in 16) into a parent element; returns the created root element and a list of errors with file name and line; a document with errors loads what it could; unknown tags, attributes and malformed markup are errors, never silently ignored.
- A document can be loaded several times (each load creates its own elements).

**Blocked by:** 09

**Touches:** `packages/FenrirUI/`

**Status:** ready-for-agent

- [ ] Parser tests: a document with nested panels, labels with text content, an image, classes and ids loads to the expected tree and resolved attributes; entities decode; comments are skipped; an include splices in place and a recursive include is one error; each error kind (unknown tag, unknown attribute, text in an element with no text, unclosed tag, bad entity) reports its line and the rest still loads.
- [ ] Headless App test: a loaded document with an inline sheet lays out as the same tree built from code.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
