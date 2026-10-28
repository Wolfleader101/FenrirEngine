Status: ready-for-agent

# DebugDraw options: the public options hold their mirror, so their defaults are the description's

Origin: candidate 8 of the 2026-10-28 architecture review (pass 3, tip `978468bf`), "DebugDraw's options have one default per field", grilled on 2026-10-28. One ticket.

The vocabulary is the repo's: *binding*, *mirror*, *API description*, *shared struct*, *facade*, *native function* from `GLOSSARY.md`; *interface*, *locality*, *the deletion test* from the codebase-design skill.

## Problem statement

C#'s `ShapeOptions` and `TextOptions` (`packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs`) are the public options a script fills. Each holds a `string? Group`, which no mirror can hold, so ADR-0043 made the natives take the options without the group, as `Fenrir::ScriptOptions::ShapeOptions` and `TextOptions` (`packages/FenrirDebugDraw/Scripting/src/ScriptOptions.hpp`), described from the binding's own selection and mirrored `internal` as `NativeShapeOptions` and `NativeTextOptions` through the binding's `internal-structs` list, with the group as text of its own.

The option fields are therefore spelled four times: the public C# struct (with default values: `Thickness = 1.0f`, `Segments = 24`, `Size = 16.0f`), the hand-written converting constructor of each internal mirror (`Options.cs`, field by field), the described C++ struct (with the same default values, which the generated mirror carries), and the C++ conversion to the engine's `Fenrir::ShapeOptions` (`DebugDrawScriptingState.hpp`, `Target::Shape` and `Text`). The default values live in two of them, the public struct and the described struct, and nothing ties them together but the end-to-end test: `Scripting/Tests/Shapes.cpp` draws every shape from C# with no options and compares the tessellation with C++'s `ShapeOptions{}`, which covers thickness, depth, segments and text size but not `Duration` or `RelativeTo`. The 22 shape and 4 text methods of the facade's hand-written part (`DebugDraw.cs`) each spend two statements converting and splitting before the one call that differs.

Facts settled during the grilling, which bound the solution:

- There is no generated defaults test for this binding: it has no `[tests]`, and an `internal-structs` mirror gets no case (a public test method cannot return one). The review's claim that such a test checks the mirror but not the public struct was wrong on its first half.
- The engine's `Fenrir::ShapeOptions` cannot be the described struct: it holds a `std::string_view group`, a kind describe-api does not record, and ADR-0042 rules out changing an engine type for a binding. Text inside a described struct can only be a fixed buffer, as `Fenrir::Name` is, which would change the public API.
- Iris.Bindings cannot generate the public options today: a `@facade` mark is refused on a native taking an `internal-structs` type, facade parameters are "as declared", and a struct by value that is not a wrapper is refused. A rule "this field is text, passed beside the struct" would be a one-package feature in two tools, which ADR-0043 rejected for the analogous `InputClaims` refresh hook, and it would land in the facade writer candidate 3 is changing. Rejected.
- `Target::Shape` and `Text` are not copies: they also take the `relativeTo` the native guard resolved and clamp `depth`. They stay.

## Solution

### The public options hold the mirror

Each public options struct keeps its `string? Group = "Default"` and gains one `internal` field holding the generated mirror, `NativeShapeOptions Native = new()` (`NativeTextOptions` for text). Every other public member becomes a property forwarding to that field: `public float Thickness { get => Native.Thickness; set => Native.Thickness = value; }`, and so on for `Duration`, `RelativeTo`, `Depth`, `Segments` and `Size`. The explicit parameterless constructor stays, so `new ShapeOptions()` runs the initialisers. Object initialisers (`new ShapeOptions { Group = "Shapes", Thickness = 3.0f }`) work unchanged over properties, so no script changes.

`new ShapeOptions()` is then the description's default by construction: the only default values in C# are the generated mirror's, which `fenrir-generate-bindings` writes from `ScriptOptions.hpp`. The two hand-written converting constructors (`NativeShapeOptions(in ShapeOptions)`, `NativeTextOptions(in TextOptions)`) are deleted. The deletion test the review asked for passes: deleting them concentrates "the options cross as a mirror plus text" in the public struct's shape, instead of smearing it over a constructor per struct.

The public docs stay on the public members: the properties carry the long docs a script reads (what duration 0 means, what `RelativeTo` does), and the described C++ fields keep their one-line docs, which the mirror carries. The `Native` field's doc says it is the options as the natives take them.

### The facade's shape and text methods

`DebugDraw.cs`'s `Shape(options, out group)` and `Text(options, out group)` become two pairs of one-line helpers, `Native(ShapeOptions?)` (`options?.Native ?? new()`) and `Group(ShapeOptions?)` (`options?.Group ?? "Default"`), and the same for `TextOptions?`. Each of the 26 methods becomes one expression-bodied line passing `Native(options)` and `Group(options)` beside its other arguments. The `in` keyword is left off the `Native(options)` argument: C# passes an rvalue to an `in` parameter by copying it to a temporary, and refuses an explicit `in` on one. The methods remain hand-written, as ADR-0043 says a facade's non-pass-throughs are; nothing here asks the generator for anything.

### ADR-0043 amended

Two sentences, in the ADR's italic *Amended by this change* style, applied in the ticket so the ADR changes with the code:

- In the decision "A binding's private plain data is described and mirrored `internal`": *Amended by this change (debug draw options): the public type over an internal mirror holds it as a field and forwards to it, so its defaults are the description's and nothing converts between them; this is the rule for any `internal-structs` type a public type stands for.*
- In the decision "A facade stays hand-written C# over generated natives", after "a real conversion (DebugDraw's options)": *Amended by this change (debug draw options): DebugDraw's hand-written part no longer converts; it holds the null-to-default and the group split, which a description cannot say either.*

No new ADR, and no glossary change: no new concept is named. The glossary's *facade* entry still lists "defaults and conversions" among what a hand-written part may hold, which stays true in general.

### Out of scope

- A generator rule for text beside a described struct, or options as a generated facade parameter (rejected above; if candidate 3's work ever makes an `internal-structs` parameter describable, this spec's shape is unaffected, since the public struct would simply hold a generated field the facade passes).
- A defaults test for the internal mirrors (needs the generator to emit a case for an internal type; the end-to-end tessellation test plus construction-by-mirror covers the drift this spec removes).
- Any change to `ScriptOptions.hpp`, `DebugDrawScriptingState.hpp`, the natives, the binding configuration's lists, or the engine's `Fenrir::ShapeOptions`.
- Other bindings' public types over internal mirrors: there are none today (the Iris.Bindings fixture's `Tag` is a test fixture).

## Decisions

1. The public options hold the generated mirror and forward to it (shape (b) of the grilling); the converting constructors go.
2. The 26 facade methods shrink to one line each in the same ticket.
3. Public docs stay on the public properties; the description's docs stay terse.
4. No new test.
5. ADR-0043 is amended with the two sentences above, stated as the rule for any `internal-structs` type; no new ADR, no glossary entry.
6. The candidate's generator shapes are rejected and recorded here, not in an ADR: the ADR already records why one-package generator rules are refused.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `Options.cs`, `DebugDraw.cs`, the binding configuration's comments, ADR-0043's two sentences | none |

One ticket, cut from `dotnet-scripting`. Branch `debugdraw-options-01`. It touches no file candidate 2's or candidate 3's work touches (Iris.Bindings, FenrirAssets).

## Done looks like

- `grep -n "NativeShapeOptions(\|NativeTextOptions(" packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs` finds nothing (no converting constructors); `grep -c "= 1.0f\|= 24\|= 16.0f" packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs` prints 0.
- `grep -c "out string group" packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/DebugDraw.cs` prints 0.
- `grep -c "debug draw options" docs/adr/0043-native-resources-and-facades-move-onto-the-generated-api-pipeline.md` prints 2.
- Every FenrirDebugDraw scripting test passes under the msvc-x64-debug preset, with `Scripting/Tests/Fixture/Calls.cs` and `Shapes.cpp` unchanged; `fenrir-generate-bindings` is a no-op after the commit; `examples/RendererDemo/scripts/DebugDrawScene.cs` is unchanged and compiles in CI.
