# 07: Port `Fenrir.DebugDraw` onto the generated pipeline

**What to build:** DebugDraw's native declarations and registration are generated, its two wire option structs are described binding-side and mirrored `internal`, and `DebugDraw` is a hand-written facade over typed natives (spec: "Each port"; ADR-0043, ADR-0044). It carries the step that closes the spec, for whichever port lands last.

- **Binding-owned selection** (`packages/FenrirDebugDraw/Scripting/api/FenrirDebugDrawScripting.api.toml`, header a new `src/ScriptOptions.hpp` holding today's `ScriptShapeOptions` and `ScriptTextOptions`): both as `shared-struct`; `DepthMode` described through the shape options (its field becomes the enum, not the widened `std::uint32_t`: the padding worry that widened it is gone, since the description records the offsets). `RelativeTo` stays the entity's 32-bit value (a `Fenrir::ScriptQueries`-style `EntityValue` bound to C# `Entity`, or an `Entity` field under `[natives] types`; the ticket follows physics). FenrirDebugDraw's own headers select nothing: C# holds no component or resource of the package.
- **Binding** (`FenrirDebugDraw.binding.toml`): `[uses]` the math binding (`Colour`, `Vector3`, `Quaternion`, `Matrix4x4`, `Bounds`); `internal-structs` the two option structs, `[names]` them `NativeShapeOptions` and `NativeTextOptions`; `[natives] sources = ["../src/DebugDrawScripting.cpp"]`, class `DebugDrawNative`; `[registration]` (shared structs and natives); `[tests]` (natives only: the defaults test still covers the two internal structs).
- **Natives:** each takes `std::string_view` for the group and text (kept), `const Colour*`, `const Math::Vec3*`, `const Math::AABB*`, `const Math::Mat4*` as it does (by pointer, `in` from C#), and the options by pointer; `KeyedMessage`'s key stays `std::int64_t`. The warn-once record moves to a `DebugDrawScripting` state class.
- **Facade:** `DebugDraw` rewritten over the generated `DebugDrawNative`: every overload, `ShapeOptions`/`TextOptions` (with `string? Group`) and the `Shape(options, out group)` conversion to the internal mirror kept; `NativeShapeOptions`/`NativeTextOptions` constructors become partial parts of the generated internal mirrors.
- **Deleted:** the hand-written internal structs' fields, `DebugDrawNative`'s declarations, the `extension.functions` list, the `static_assert`s on sizes (the description holds them).
- **Closing the spec:** remove the "(the extensions of input, window, assets, renderer and debug draw still declare both by hand)" note under *Native function* in `GLOSSARY.md`; the commit that marks this ticket resolved deletes `.scratch/scripting-generated-extensions/`.

**Blocked by:** 03 (`Fenrir.Math` through a math binding, and physics's `Aabb` becomes `Bounds`)

**Touches:** `packages/FenrirDebugDraw/Managed/`, `packages/FenrirDebugDraw/Scripting/`, `GLOSSARY.md`

**Status:** ready-for-agent

- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `[NativeFunction]` or `NativeFunction::Make` remains under the package.
- [ ] The existing debug-draw extension tests pass unchanged in behaviour (items, shapes, text, groups, no-op rules); the generated natives and defaults tests run in the test target.
- [ ] `DebugDraw.Line(a, b, Colour.Red)`, `DebugDraw.Box(bounds, colour, new ShapeOptions { Group = "nav", Depth = DepthMode.OnTop })` read exactly as before from a script.
- [ ] Only the ticket's own Touches change. If 06 lands after this ticket, the glossary note and the directory deletion move to 06's closing commit instead.
