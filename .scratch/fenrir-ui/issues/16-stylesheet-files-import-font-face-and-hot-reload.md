# 16: Stylesheet files, `@import`, `@font-face` and hot reload

**What to build:** documents and stylesheets load from paths, fonts are named in the sheet, and editing a file updates the running game (spec: "FenrirUI" hot reload, markup document, stylesheet; ADR-0062 registry, ADR-0064).

- **Files**: `LoadDocument(root, parent, path)` and `AddStylesheet(root, path)` read through the asset store's file access with paths relative to the asset root; `<style src>` and `<include src>` resolve relative to the including file; `@import` loads another sheet in place with loop protection.
- **`@font-face`**: `font-family`, `src: url()`, `font-weight`, `font-style` register a `Font` asset's face with FenrirText's registry under the family; the SDF opt-in descriptor is parsed and stored for ticket 22.
- **Hot reload**: a per-root switch; the root remembers every file it loaded (documents, includes, sheets, imports) with its modification time and polls them every 250 ms of FenrirTime; a changed sheet re-parses and dirties every style; a changed document reloads into the same parent at the same position, removing the previous elements at once, and sends the document-reloaded `UIEvent` so the game re-finds its ids; a file that fails to parse keeps the previous version and logs the errors.

**Blocked by:** 08, 15

**Touches:** `packages/FenrirUI/`

**Status:** resolved

- [x] Headless App tests with files in a temporary directory: a document loaded by path with a `<style src>` and an `@import` resolves both relative to their files; an import loop is one error; `@font-face` registers a family that a label then measures with; after the sheet file is rewritten and 250 ms tick past, the label's colour changes without reloading; after the document is rewritten, the new tree is in place, old ids are stale and the reload event was sent; a document rewritten with an error keeps the old tree and logs.
- [x] With the switch off, no file is polled.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
