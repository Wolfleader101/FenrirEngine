Status: ready-for-agent

# Asset handles: a native takes the handle as the bound type it already is, and a facade returns one output as a value

Origin: the third architecture review of `dotnet-scripting` (2026-10-28, tip `978468bf`), candidate 2 (asset handles cross as the bound type, not as packed bits) grilled together with candidate 3 (a native with one `out` gets a generated value-returning facade), ten decisions in two rounds, all recommendations accepted. The review's report is `architecture-review-20261028-dotnet-scripting-pass3.html` in the user's temp directory; its other candidates are not specified.

The vocabulary is the repo's: *native function*, *facade*, *bound type*, *shared struct*, *binding*, *API description*, *asset handle*, *wrapper*, *native guard* from `GLOSSARY.md`; *module*, *interface*, *seam*, *locality*, *leverage* from the codebase-design skill.

## Problem statement

**A handle crosses a native in a form nothing else uses.** ADR-0044 left `AssetHandle<T>` crossing a native function "as the one 64-bit number its two fields pack into, with the facade packing". So 39 parameters and results of the 36 assets natives (`packages/FenrirAssets/Scripting/src/AssetsScripting.cpp`) and FenrirRenderer's `SpawnModel`'s model are `std::uint64_t` where the C++ API takes `AssetHandle<T>`, and the packing is spelled in six places: `AssetHandleBits.hpp` (`AssetHandleBits`, `AssetHandleFromBits`, used 47 times in the natives), `AssetResolve.hpp` (`Resolve<T>` takes bits), `Handles.cs` (`Bits`, `FromBits`, 43 uses across `Assets.cs`, `Materials.cs`, `Shaders.cs` and `Renderer.cs`), the renderer's native, the assets fixture (`Bits.Of`/`Bits.To`) and the C++ test apps (`BitsOf`/`HandleFrom`, `1 << 32` in three no-op tests). The same handle is already a bound type of the assets binding (`[bound-types]` in `api/FenrirAssets.binding.toml`, mapped by hand in `AssetMappings.hpp`), checked by Iris wherever a component or shared struct holds one; and the pipeline already passes a bound type by pointer (physics natives take `const Math::Vec3*` as `in Vector3`). The precedent is the entity: `Fenrir::Entity` stopped crossing as a packed wrapper in the entity-fields spec, and `EntityValue` and `EntityLink` went with it.

**`Assets.cs` exists to pack.** All nine of its methods, and 25 of `Materials.cs`'s, are one-line pass-throughs whose only work is `.Bits` and `FromBits`; with the packing gone they would be `@facade` marks, except that six of them return a handle a native gives through an output.

**A native with one output cannot be a pass-through.** Natives of the shape `bool F(inputs..., T* out)` serve a C# method that returns `T`: a handle, an entity, a size, a joint state. The `@facade` mark generates the declaration's shape, `bool F(..., out T)`, so each contract writes the same four lines by hand (call, drop or branch on the bool, return the out): the six assets returners, `Renderer.CameraAt` and `SpawnModel`, `Window.Size` and `PixelSize`, `PhysicsWorld.GetJointState`. Five marked natives have the same C++ shape and keep the bool on purpose (`Renderer.ScreenPointToRay`, `WorldToScreenPoint`, `GetWorldBounds`, `Stats`, and one in the fixture), so the value-returning form cannot be inferred from the shape.

## Solution

### A native takes and gives a handle as `AssetHandle<T>`

A native that takes a handle takes `const AssetHandle<T>*` (C# `in AssetHandle<T>`); one that gives a handle writes it through `AssetHandle<T>*` marked `@param[out]` (C# `out AssetHandle<T>`) and returns `bool`. The pointee is the bound type the binding already names, so Iris.Bindings declares it as it declares `in Vector3` today, and the registration zeroes the output before the native runs (ADR-0045), so a failed call leaves the empty handle. `AssetResolve.hpp`'s `Resolve<T>` takes `const AssetHandle<T>&`; `AssetHandleBits.hpp` is deleted, with `Bits` and `FromBits` in `Handles.cs` and every "packed as Fenrir.Assets packs one" sentence in the natives' docs. Nothing in `AssetHandle<T>`'s C++ or C# layout changes, and nothing in the mirrors that hold one (`MeshRenderer`, `MaterialTexture`).

The first thing ticket 02 verifies: describe-api spells the pointee of `const AssetHandle<Mesh>*` as `Fenrir::AssetHandle<Fenrir::Mesh>`, the key the binding's `[bound-types]` already uses, so `Natives.Pointee` in Iris.Bindings resolves it through `binding.Find`. If clang spells the specialisation otherwise, describe-api's `DeclaredPointee` (which keeps an alias as written) is where the spelling is settled, and the ticket says what it found.

### The three kind-generic natives take a kind-neutral handle

`Load`, `Unload` and `IsValid` take `Detail::AssetKind` and dispatch at run time through `ForKind`, serving C#'s generic `Load<T>`, `Unload<T>` and `IsValid<T>`; no one `AssetHandle<T>` can be their pointee. They take `const ScriptAssetHandle*` and `ScriptAssetHandle*` instead: `struct ScriptAssetHandle { std::uint32_t index = 0; std::uint32_t generation = 0; }` in `ScriptAssets.hpp`, selected as a shared struct by `FenrirAssetsScripting.api.toml`, mirrored `internal` as `NativeAssetHandle` through `internal-structs` and `[names]` (the pattern ADR-0043 made for a binding's private plain data), and converted to and from `AssetHandle<T>` by `ForKind`'s action on the C++ side and by `internal NativeAssetHandle ToNative()` / `internal static AssetHandle<T> FromNative(NativeAssetHandle)` on `AssetHandle<T>`, which replace `Bits` and `FromBits`. The 36 other natives take the typed handle. `AssetKind<T>` in `Handles.cs` stays as the one generic-to-kind bridge.

### A facade mark may name the output the method returns

`@facade Class.Method returns <name>` names one of the native's single `out` parameters. describe-api records it as the mark's `returns`; it refuses a `returns` naming a parameter that is not a single `out` (an output span, an `inOut`, an input), a native with more than one single `out`, and a native whose result is not `void` or `bool`. Iris.Bindings writes the method with the out's C# type as its result and without that parameter: the body declares the out, calls the native, discards a `bool` result and returns the out **exactly as the call left it**. The registration zeroed it and a native that fails returns without writing it (ADR-0045), so a failed call returns the C++ default: the empty handle, `Entity::None` (which is not C#'s `default(Entity)`), a zero `WindowSize`. Generated code carries no `? value : default` and the generator knows no per-type "none"; the method's doc ends "the C++ default when the call did nothing". The plain `@facade Class.Method` form is unchanged, and the five marked natives that keep their bool are untouched. The mark on a mirror (`@facade Entity.Method`) takes `returns` the same way.

### What stays hand-written

A generated facade method takes exactly what the declaration takes, so overloads with default arguments stay hand-written over the generated methods, as ADR-0043 keeps "overloads that are not renames": `AddMesh`, `AddImage`, `AddRenderTarget`, `Materials.Create` and `Clone` default `owner` to `AssetOwner.Scene` and keep a one-line overload each; `Joints.FrameFromWorld`'s one-body overload stays. Generic methods (`Load<T>`, `Unload<T>`, `IsValid<T>`) and properties (`Mouse.Position`, `Motion`, `Scroll`) stay. `Entity.Create` and `Entity.WorldTransform` throw on failure and stay. `Materials.TryGet*` keep their bool and out (the caller wants both) and become plain `@facade` marks once their handle parameter is `in AssetHandle<Material>`.

### Tests

Iris's `FindMethod` invoke passes numbers and pointers, not structs, so a fixture method that takes or gives a handle still carries it as two 32-bit halves in one `ulong`. That packing becomes the test harness's own convention, named as such and spelled once per side: the fixture's `Bits` class (renamed `TestHandle`, "how the tests carry a handle across Iris's scalar-only invoke; not how a native takes one") and the C++ test app's `BitsOf`/`HandleFrom` (the same comment). The no-op tests build stale handles as `AssetHandle<T>{index, generation}` and pass pointers, as the natives now take them.

### Out of scope

- Struct-by-value crossing in Iris (ADR-0044's open door): nothing here needs it; the pointer rule serves.
- Default arguments or properties in generated facades.
- The other review candidates.

## Decisions (from the grilling)

1. Handles cross a native by pointer as the bound type; the packing goes. ADR-0044's "what stays" clause is amended inline, as its two Entity amendments were, and the glossary's *wrapper* and *asset handle* entries with it; no new ADR.
2. The three kind-generic natives take the kind plus a described kind-neutral `ScriptAssetHandle`, mirrored internal as `NativeAssetHandle`; `AssetHandle<T>` converts to it. Not fifteen typed natives, not a packed `ulong` for three.
3. Candidate 3 is part of this spec: the generator rule leads, in a ticket of its own, and every hand-written method of the shape that returns the C++ default on failure becomes a mark.
4. Default-argument overloads, generic methods and properties stay hand-written; the generator learns one rule.
5. The tests keep a harness-only packing for Iris's scalar invoke, named as the harness's.
6. The mark's opt-in is `@facade Class.Method returns <out>`, refused where it cannot hold; the shape is never inferred.
7. A value-returning facade returns the out as the call left it and drops the bool; no C#-side sentinel.
8. Names: `Fenrir::ScriptAssetHandle`, C# `NativeAssetHandle`, `ToNative()` / `FromNative()`.
9. Three tickets: 01 the generator rule; 02 assets and renderer; 03 window and physics marks. 02 and 03 are disjoint and run side by side off 01.
10. The ADR amendment and glossary change land in the spec PR; READMEs and doc files with the tickets that change the code.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | describe-api records `returns` on the mark and refuses what cannot hold; Iris.Bindings writes the value-returning facade; docs, README, fixture cases | none |
| 02 | The assets and renderer natives by pointer, `ScriptAssetHandle`, `AssetHandleBits.hpp` deleted, the facades shrunk to marks and overloads, tests on the harness's packing | 01 |
| 03 | `returns` marks on `Window.Size`, `PixelSize` and `PhysicsWorld.GetJointState`; their hand-written methods deleted | 01 |

Branches `asset-handles-01-facade-returns`, `-02-assets-by-pointer`, `-03-window-physics-marks`; 02 and 03 each cut from 01's branch, each PR targeting it.

## Done looks like

- `grep -rn "AssetHandleBits\|FromBits\|\.Bits\b" packages/` finds nothing outside `Tests/`; `packages/FenrirAssets/Scripting/include/FenrirAssetsScripting/AssetHandleBits.hpp` does not exist; `grep -c "std::uint64_t" packages/FenrirAssets/Scripting/src/AssetsScripting.cpp` is 0.
- `grep -c "returns" packages/FenrirAssets/Scripting/api/FenrirAssets.natives.json` is 6; `Assets.cs` holds the three generic methods and the three defaulted overloads and nothing else; `Window.cs` holds `Main` alone; `Renderer.cs` holds the one-argument `SpawnModel` overload alone.
- Iris.Bindings' fixture has one native marked `returns`, its generated facade returns the out's type, and a `returns` on a bool-and-out native the fixture keeps marked plain still generates `bool F(..., out T)`; describe-api's failing natives cover the three refusals.
- `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; every scripting test passes under the msvc-x64-debug preset after each ticket; the other presets are CI's.
