# 02: The assets and renderer natives take a handle as `AssetHandle<T>`, and the packing goes

**What to build:** the spec's "A native takes and gives a handle as `AssetHandle<T>`", "The three kind-generic natives take a kind-neutral handle" and "Tests", with the six returners and `CameraAt`/`SpawnModel` on `returns` marks (ticket 01).

- **First check**: describe `packages/FenrirAssets/Scripting/src/AssetsScripting.cpp` with one native changed to `const AssetHandle<Mesh>*` and confirm `FenrirAssets.natives.json` spells the pointee `Fenrir::AssetHandle<Fenrir::Mesh>` (the `[bound-types]` key); if clang spells it otherwise, settle it in describe-api's `DeclaredPointee` and say so in the PR.
- **`packages/FenrirAssets/Scripting/include/FenrirAssetsScripting/ScriptAssets.hpp`**: `struct ScriptAssetHandle { std::uint32_t index = 0; std::uint32_t generation = 0; }` with a comment saying it is the kind-generic natives' handle, converted to `AssetHandle<T>` once the kind is known. `api/FenrirAssetsScripting.api.toml` selects it as `shared-struct`; `api/FenrirAssets.binding.toml` lists it under `[csharp] internal-structs` and names it `NativeAssetHandle` in `[names]`.
- **`AssetsScripting.cpp`**: every `std::uint64_t` handle parameter becomes `const AssetHandle<T>* name` and is dereferenced (no null check, ADR-0045); every `std::uint64_t` result becomes `bool` with an `AssetHandle<T>* name` output marked `@param[out]`, written on success. `Load`, `Unload` and `IsValid` take `const ScriptAssetHandle*` / `ScriptAssetHandle*`, and `ForKind`'s action builds `AssetHandle<T>{handle->index, handle->generation}` or writes one back. `ReadInto` takes `const AssetHandle<Material>&`. `@facade AssetsNative`-class marks: `Load` stays unmarked (generic in C#); `AddMesh`, `AddRenderTarget`, `AddImage` get `@facade Assets.<Name> returns <out>`; `CreateMaterial`/`CloneMaterial` get `@facade Materials.Create returns material` / `Materials.Clone returns material`; `ReplaceMesh`, `ResizeRenderTarget`, `ReplaceImage`, every `Set*`, `TryGet*`, `IsSet`, `Reset`, `GetShaderParameterCount`, `GetShaderParameter` get plain `@facade` marks on `Assets`, `Materials` or `Shaders`. Every "packed" sentence in the docs goes.
- **`AssetResolve.hpp`**: `Resolve<T>(guard, const AssetHandle<T>& handle, where)`; the include of `AssetHandleBits.hpp` goes. **Delete `AssetHandleBits.hpp`** and its line in `Scripting/CMakeLists.txt`.
- **`packages/FenrirAssets/Managed/Fenrir.Assets/Handles.cs`**: `Bits`/`FromBits` become `internal NativeAssetHandle ToNative() => new(Index, Generation)` and `internal static AssetHandle<T> FromNative(NativeAssetHandle native) => new(native.Index, native.Generation)` (member names as the mirror generates them). **`Assets.cs`** keeps `Load<T>`, `Unload<T>`, `IsValid<T>` (over `ToNative`/`FromNative`) and the three `owner`-defaulted overloads calling the generated methods; **`Materials.cs`** keeps `Create`/`Clone` overloads and the class doc; **`Shaders.cs`** keeps the class doc; each becomes `partial` beside its generated part. Docs that said "the empty handle" on failure still hold.
- **`packages/FenrirRenderer/Scripting/src/RendererScripting.cpp`**: `SpawnModel` takes `const AssetHandle<Model>* model`, `Resolve<Model>(guard, *model)`, marked `@facade Renderer.SpawnModel returns root`; `CameraAt` marked `@facade Renderer.CameraAt returns camera`. **`Renderer.cs`** keeps the class doc and the one-argument `SpawnModel` overload. The includes of `AssetHandleBits.hpp` go.
- **Tests**: `packages/FenrirAssets/Scripting/Tests/Fixture/Meshes.cs`'s `Bits` becomes `TestHandle` with the spec's comment; `AssetsScriptingApp.hpp`'s `BitsOf`/`HandleFrom` get the same comment; `NoOps.cpp` (assets and renderer) build stale handles as `AssetHandle<T>{index, generation}` and pass pointers, and the `1 << 32` lines go. Rules and .NET tests pass unchanged in meaning.
- **Regenerate** both stages; commit the generated diff (natives header, registration, `NativeAssetHandle.cs`, the mappings, the generated `Assets.cs`/`Materials.cs`/`Shaders.cs`/`Renderer.cs` parts, the renderer's natives). `packages/FenrirAssets/Scripting/README.md` (if any) and `ScriptAssets.hpp`'s header comment describe the new shape.

**Blocked by:** 01

**Touches:** `packages/FenrirAssets/Scripting/`, `packages/FenrirAssets/Managed/Fenrir.Assets/`, `packages/FenrirRenderer/Scripting/src/RendererScripting.cpp`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirRenderer/Scripting/Tests/NoOps.cpp`, `packages/FenrirRenderer/Managed/Fenrir.Renderer/`

**Status:** claimed

- [x] `AssetHandleBits.hpp` does not exist; `grep -rn "FromBits\|\.Bits\b\|AssetHandleBits" packages/FenrirAssets packages/FenrirRenderer` finds nothing outside `Tests/`; `grep -c "std::uint64_t" packages/FenrirAssets/Scripting/src/AssetsScripting.cpp` is 0.
- [x] `Assets.cs` holds six methods (three generic, three overloads); `Renderer.cs` holds one; both stages regenerate to a clean tree.
- [x] `FenrirAssetsScripting`, `FenrirAssetsScriptingRules`, `FenrirRendererScripting` and `FenrirRendererScriptingRules` pass under the msvc-x64-debug preset.
- [x] `check_tickets.py` and clang-format pass.

## Comments

Filed from the asset-handles grilling (spec decisions 1, 2, 4, 5, 8). Cut from ticket 01's branch; disjoint from ticket 03.
