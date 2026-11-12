# 19: Scripting: text and painter facades

**What to build:** a C# system paints shapes and text on the window overlay (spec: "Scripting"; ADR-0061, ADR-0042).

- **FenrirText's binding**: an API description, generated binding and contract; fonts registered by path and family, paragraphs as ids built through natives (set text and styles, lay out to a width, read size, line metrics and hit positions).
- **FenrirPainter's binding**: the painter as a facade over a painter id a script obtains from the overlay, by window and layer, for the frame (`Painter.Overlay`: the extension owns one `DrawList2D` per window and layer, records into it through the painter and moves it into the app's `Overlay` resource when the calling system returns; no C# draw list and no submit); one native per draw and state call; paths as handles built through natives or as spans of points; paints and stroke styles as `in` shared structs; colours as FenrirMath mirrors and rectangles as `Rect2D` mirrors; `DrawText` with a paragraph id. All C# is generated.
- Contracts deployed beside `Fenrir.Scripting.dll`; the generated tests the binding pipeline produces, plus a script test.

**Blocked by:** 06

**Touches:** `packages/FenrirText/Scripting/`, `packages/FenrirText/Managed/`, `packages/FenrirPainter/Scripting/`, `packages/FenrirPainter/Managed/`, `packages/FenrirScripting/`

**Status:** resolved

- [x] Scripting tests: a script lays out a paragraph and reads its size; a script paints a rounded rectangle, a stroked path and a word through `Painter.Overlay`, and the null-backend renderer's `Overlay 2D` pass draws what the same C++ calls draw; the no-Scripting-running rules return zero and do nothing.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- Built (2026-11-12). A painter from `Painter.Overlay` lasts while the C# system that got it runs: the extension records the list's submission in the call context's change buffer, so the list reaches the `Overlay` when that system's changes play back (as it returns, or once per group in a hosted stage, whose systems then share the painter), before the renderer draws the frame. Calls before then for the same window and layer share the painter; a later system gets a new one. A painter whose changes were discarded (its system threw) is dropped, unsubmitted, at the next frame's first `Painter.Overlay`, so it keeps no layer. A frame-end hook was not used: the extension has no App at its making to add a system, and one added on first use (through the change buffer) would run after the renderer's own Render system and land a frame late.
- `Painting::ScriptingExtension(text)` takes the text extension (`Text::ScriptingExtension()`) and shares its state through the new `ScriptingExtension::GetState<T>()`, so `Painter.DrawText` takes the generated `Paragraph` and no C# is written by hand. The app adds the `FontLibrary` as an app resource over its asset store; ticket 20's UI should use the same one.
- Ids cross by pointer (`in PainterId`, `in Path2D`, `in Paragraph`), not as wrappers, which would need a hand-written `Iris::WrapperOf` each. `Rect2D` (FenrirDraw2D's) is mirrored by the text binding, as FenrirMath has no rectangle; the painter's binding names it. A script's gradient of more than two stops draws with its end stops (no stop texture yet).
- describe-api now records a default that names a constant of the nested struct's type (`Colour::Black`), which the painter's plain data uses.

