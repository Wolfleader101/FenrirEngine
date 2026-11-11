# 19: Scripting: text and painter facades

**What to build:** a C# system paints shapes and text into a list and submits it to the overlay (spec: "Scripting"; ADR-0061, ADR-0042).

- **FenrirText's binding**: an API description, generated binding and contract; fonts registered by path and family, paragraphs as wrapper handles built through natives (set text and styles, lay out to a width, read size, line metrics and hit positions).
- **FenrirPainter's binding**: the painter as a facade over a painter handle a script obtains for a draw list it owns (a mirrored `DrawList2D` already crosses); one native per draw and state call; paths as handles built through natives or as spans of points; paints and stroke styles as `in` shared structs; colours and rectangles as FenrirMath mirrors; `DrawText` with a paragraph handle; the list submitted to the overlay as today.
- Contracts deployed beside `Fenrir.Scripting.dll`; the generated tests the binding pipeline produces, plus a script test.

**Blocked by:** 06

**Touches:** `packages/FenrirText/Scripting/`, `packages/FenrirText/Managed/`, `packages/FenrirPainter/Scripting/`, `packages/FenrirPainter/Managed/`, `packages/FenrirScripting/`

**Status:** ready-for-agent

- [ ] Scripting tests: a script lays out a paragraph and reads its size; a script paints a rounded rectangle, a stroked path and a word into a list, submits it, and the C++ side's captured list holds the expected commands; the no-Scripting-running rules return zero and do nothing.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
