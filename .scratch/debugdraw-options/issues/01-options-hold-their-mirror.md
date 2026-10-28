# 01: the public options hold their mirror, and the facade's shape and text methods shrink

**What to build:** `ShapeOptions` and `TextOptions` hold their generated internal mirror and forward to it, so `new ShapeOptions()` is the description's default and the converting constructors go; the 26 shape and text methods of `DebugDraw` become one line each; ADR-0043 gains its two sentences (spec: all sections).

- **`packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs`**: in `ShapeOptions`, keep `public string? Group = "Default";` and the explicit `public ShapeOptions() {}`; add `internal NativeShapeOptions Native = new();` with a doc saying it is the options as the natives take them (`ScriptOptions.hpp`'s struct, generated into `Generated/NativeShapeOptions.cs`), without the group; turn `Duration`, `RelativeTo`, `Thickness`, `Depth` and `Segments` into properties `{ get => Native.X; set => Native.X = value; }`, each keeping its current doc comment word for word (the defaults the docs name, 1 and 24, are now the mirror's). The same for `TextOptions` with `Duration`, `RelativeTo` and `Size` over `NativeTextOptions`. Delete both `internal partial struct` blocks with their converting constructors and the comment above them; the file's struct docs stop saying "converting" and say the options hold the natives' form.
- **`packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/DebugDraw.cs`**: replace `Shape(ShapeOptions?, out string)` and `Text(TextOptions?, out string)` with four one-line private helpers: `Native(ShapeOptions? options) => options?.Native ?? new()`, `Group(ShapeOptions? options) => options?.Group ?? "Default"`, and the two over `TextOptions?`. Each of the 22 shape methods and 4 text methods becomes one expression body, its native call with `Native(options)` where `in native` was and `Group(options)` where `group` was, with no `in` keyword on the `Native(options)` argument (C# copies an rvalue into a temporary for an `in` parameter and refuses an explicit `in` on one). Public signatures, docs and parameter order are unchanged. The file's header comment stops saying the options "convert to the natives' internal structs" and says the public options hold them.
- **`packages/FenrirDebugDraw/Scripting/api/FenrirDebugDraw.binding.toml`**: the comment above `internal-structs` ("C# names ShapeOptions and TextOptions, with their group, and converts them to these") says the public types hold these; the header comment's "the options the natives take are mirrored internal for it" stands.
- **`docs/adr/0043-native-resources-and-facades-move-onto-the-generated-api-pipeline.md`**: the two *Amended by this change (debug draw options)* sentences the spec gives, one at the end of the `internal-structs` decision bullet and one in the facade decision bullet after "a real conversion (DebugDraw's options)".
- **Build and test**: the msvc-x64-debug preset; `ctest -R DebugDraw` (the FenrirDebugDraw scripting tests: shapes, text, items, no-ops) passes with `Scripting/Tests/Fixture/Calls.cs` and `Scripting/Tests/Shapes.cpp` unchanged. `fenrir-generate-bindings` is a no-op (nothing described changes). If the SDK's `[NativeFunction]` generator refuses the rvalue argument for an `in` parameter, bind it to a local first (`NativeShapeOptions native = Native(options);`) and keep the one-call shape; say so in the PR.

**Blocked by:** none

**Touches:** `packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs`, `packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/DebugDraw.cs`, `packages/FenrirDebugDraw/Scripting/api/FenrirDebugDraw.binding.toml`, `docs/adr/0043-native-resources-and-facades-move-onto-the-generated-api-pipeline.md`

**Status:** ready-for-agent

- [ ] `grep -n "NativeShapeOptions(\|NativeTextOptions(" packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs` finds nothing; `grep -c "= 1.0f\|= 24\|= 16.0f" packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/Options.cs` prints 0.
- [ ] `grep -c "out string group" packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/DebugDraw.cs` prints 0; every shape and text method is one expression body.
- [ ] `grep -c "debug draw options" docs/adr/0043-native-resources-and-facades-move-onto-the-generated-api-pipeline.md` prints 2.
- [ ] `ctest -R DebugDraw` passes under the msvc-x64-debug preset; `fenrir-generate-bindings` is a no-op; `check_tickets.py` and clang-format pass.

## Comments

Filed 2026-10-28 from the grilling of candidate 8 of the pass-3 architecture review. Cut from `dotnet-scripting`. No other open ticket touches these files; candidates 2 and 3 (asset handles, value-returning facades) work in Iris.Bindings and FenrirAssets.
