# 20: Scripting: UI handles, events and the hosted paint stage

**What to build:** a C# system builds and drives a UI, reads its events and paints into an element it owns, with UIDemo's speedometer as the proof (spec: "Scripting", "UIDemo" C# variant; ADR-0061, ADR-0039).

- **FenrirUI's binding**: `ElementId` as a wrapper struct; UI roots as handles made through the UI facade; natives for create, remove (deferred and immediate), parent and reorder, tag, id, classes, inline style by name and value, text, control values, laid-out rectangles, queries by id and class and children, focus, loading documents and stylesheets by path; `UIEvent` mirrored as a shared event read through `EventReader<UIEvent>`.
- **The hosted stage** `Fenrir.UI.Paint`, after layout and before painting, in which a C# system obtains a painter for an element it owns (the UI keeps a per-element list the C++ paint pass appends) and paints through ticket 19's facade.
- **UIDemo**: with scripting on, a C# system draws a speedometer element on the HUD and reads menu clicks, logging them; the demo's README says so.

**Blocked by:** 16, 18, 19

**Touches:** `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/`, `packages/FenrirUI/src/`, `packages/FenrirScripting/`, `examples/UIDemo/`

**Status:** resolved

- [x] Scripting tests: a script builds the same three-panel tree a C++ test builds and reads the same rectangles; a script loads a document by path and finds an element by id; a click fed through the test feed arrives in the script's `EventReader<UIEvent>` with the target id; a script's paint-stage system's draws appear in the element's captured list after the element's own box; a stale id's natives do nothing and return false.
- [x] UIDemo builds with scripting on and the speedometer draws.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
