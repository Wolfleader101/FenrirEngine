Status: ready-for-agent

# FenrirAssets dev-time data: the project file and its layers, `Project::Load` and `Get<T>`, import sidecars and their options types, and `--project`

Origin: the serialisation map (`.scratch/fenrir-serialisation/map.md`), gathering tickets 07 (how engines hold dev-time data) and 18 (what dev-time data the engine holds), with ticket 08 for the as-is and ticket 12 for the envelope, settled in grilling sessions on 2026-11-19. It is the sixth of the map's six specs and builds on the types spec (`.scratch/fenrir-serialisation-types/spec.md`: the `settings` role, the `path` kind, `@transient`, the `Describe<T>` trait), the format spec (`.scratch/fenrir-serialisation-format/spec.md`: the value tree, the JSON5 reader, upgraders, located diagnostics) and the data-assets spec (`.scratch/fenrir-serialisation-data-assets/spec.md`: the envelope, the `AssetSource` seam, the asset watcher), and cites rather than restates them.

| ADR | Decision |
|---|---|
| 0070 | A project file layers each package's config by type name, and import sidecars hold options only |
| 0060 | Types read from and written to files are described by the API description and registered with FenrirTypes from generated code |
| 0063 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0065 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0067 | The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id |
| 0071 | The development formats promise a cooked form path identity, compiled-in descriptions, the tree encoding and one asset source seam |

## Prerequisite

The data-assets spec is built, and with it the types and format specs: `tools/describe-api` emits the `settings` role, the `path` kind and `@transient`; `tools/generate-type-registry` emits a per-type visitor reachable at compile time through `Describe<T>`; `FenrirSerialisation` reads an envelope's tree with located diagnostics and runs upgraders; `FenrirAssets` owns the envelope, reads bytes through the private `AssetSource` seam and keeps a `ReloadWatcher` per loaded file. Nothing here needs the scenes or C# specs.

## Problem Statement

Six packages take an `Install(App&, Config)` struct: `WindowConfig` (FenrirWindow), `RendererConfig` (FenrirRenderer), `PhysicsConfig` (FenrirPhysics), `AnimationConfig` (FenrirAnimation), `AssetsConfig` (FenrirAssets) and `ScriptingConfig` (FenrirScripting). Every one is filled in code: `DemoScaffold::Install` builds a `WindowConfig` from `DemoConfig::appName` and `imguiOverlay`, a `RendererConfig` from `DemoConfig::msaa` and the `FENRIR_DEMO_BACKEND` environment variable, installs `Assets` with no config at all (so the asset root is always `assets/` beside the executable), and a `ScriptingConfig` whose `scriptAssembly` is a compile-time define; each demo's `main()` takes no arguments, and the scaffold's switches are environment variables. `ScriptingConfig::scriptAssembly`'s doc comment names "the `scriptAssembly` field of the .feproj", a file nothing reads or writes.

None of the six is describable today (ticket 08): `tools/describe-api` refuses `std::string` (`WindowConfig::title`, `CollisionLayer::name`), `std::filesystem::path` (`AssetsConfig::root`, three in `ScriptingConfig`), `std::vector` (`PhysicsConfig::collisionLayers`, `ScriptingConfig::extensions`) and `std::chrono::milliseconds` (three in `ScriptingConfig`). No config struct is in any package's selection, so no visitor exists for any of them.

Imports run at load with no options. `ImportModel` (cgltf for glTF and GLB, ufbx for FBX and OBJ) hard-codes the engine's space: ufbx converts to right-handed Y-up metres by adjusting the root nodes (ADR-0031), glTF is taken as is, and OBJ, which has no axes, is assumed Y-up. Every sub-asset a file holds is imported (meshes, materials, images, skins, clips, skeletons), and a skin's skeleton root is glTF's `skin.skeleton` or the nearest non-bone ancestor. `ImportImage` (stb) marks PNG and JPEG sRGB and HDR linear, corrected only by a model texture's first use; `ImageData` has no mips and the renderer's `ImageUpload` always generates them. Sampler state comes from the material. There is no import cache, and no file beside a source changes any of this.

## Solution

All in FenrirAssets, with the `settings` role applied in each package that owns a config, and the command line in the demo scaffold (ADR-0070).

### The config structs become settings types

- **Each of the six configs joins its package's `.api.toml` selection in the `settings` role** with `@version 1`, so `tools/describe-api` describes it and `tools/generate-type-registry` emits its visitor, reachable through `Describe<T>` with no registry (types spec). Strings, paths, lists and nested structs are allowed in this role as in `data-asset`; `WindowSize`, `Math::Vec3` and `CollisionLayer` are described as nested values.
- **`ScriptingConfig::extensions` is `@transient`**: code wiring, never in a file, never set by a layer. `ScriptingConfig`'s three `std::chrono::milliseconds` fields are described as `i64` milliseconds.
- **The `path` kind is relative to the project file** for `AssetsConfig::root`, `ScriptingConfig::scriptAssembly`, `bridgeDirectory`, `dotnetRoot` and `reloadDirectory`: `Get<T>` resolves a relative path against the directory of the layer file that set it, and a command-line path against the working directory. An absolute path is kept.
- **The `.feproj` doc comment on `ScriptingConfig::scriptAssembly` is corrected** to name `Fenrir::ScriptingConfig.scriptAssembly` in the project file.

### The project file

- **`<name>.project.json5` at the project root, beside the asset root** (ADR-0070): one envelope (`format`, `type: "Fenrir::Project"`, `version`, `data`, no `sub`), its `data` a map from canonical type name (`Fenrir::RendererConfig`) to that config's fields, the shape of a scene's `resources`. Every file in the stack is a partial envelope: a type or a field left out is left to the layer below.
- **`Fenrir::ProjectConfig { name, firstScene }`** in FenrirAssets is the one settings type that is not a package's Install config: the project's name and its first scene as an asset path string the game hands to `LoadScene`. The engine reads neither.
- **Project versus scene** (ADR-0070): the project holds window, backend and quality, physics limits and defaults, the asset root, the script assembly, the project name, the first scene, and references to data assets by asset path rather than inlined values; a scene's `resources` hold the per-scene environment (sky, sky light). Device choice is never in the project file.
- **The asset root follows the project file**: with a project file, an empty `AssetsConfig::root` is `assets/` beside it; without one, `assets/` beside the executable as today.

### `Project::Load` and the five layers

- **`Fenrir::Project::Load(path, overrides, logger) -> Project`** in FenrirAssets, before any `Install` and without an `App`: reads `<name>.project.json5`, then `<name>.project.<platform>.json5` (`windows`, `linux`, `macos`, the platform the build runs on), then `<name>.project.local.json5`, then the `overrides` (`--set Fenrir::RendererConfig.vsync=false`, each a dotted field path after the type name and a JSON5 value, a bare word read as a string), into one tree merged field by field: a nested struct merges by field, a list replaces the list below it. Code defaults are the fifth layer, below them all, and belong to the caller (next bullet). No environment variables (ADR-0070). A platform or local file that does not exist is simply absent; a named project file that is missing or unreadable is one error diagnostic and a `Project` holding nothing.
- **`project.Get<T>(T defaults = T{}) -> T`** fills a config through `Describe<T>`'s visitor resolved at compile time, over the caller's code defaults, so Install signatures stay and the app's own defaults (`DemoConfig::msaa`) are the bottom layer. Every settings type is `@version 1` in this spec; a tree whose `version` is below the type's is read as it is with one warning, and when a settings type first moves its version the caller registers its upgraders on the `Project` before `Get<T>` (`project.RegisterUpgrader<T>(fromVersion, upgrader)`, the format spec's upgrader shape), since `Describe<T>` is tree-free and no registry exists yet. An unknown field warns and drops; a type the build does not know is kept opaque with no diagnostic, so one project file serves apps installing different packages (ADR-0063).
- **Provenance per field**: `Project` records which layer set each field (`Project::Layer { Default, Project, Platform, Local, CommandLine }`, read by `project.Provenance(typeName, fieldPath)`), and `Load` logs the stack once at `Info` through its logger in a development build: each layer's file found or absent, then each field set above its default with its value and layer. A cooked build logs nothing; the switch is the cooking effort's (ADR-0071).
- **Located diagnostics** (ADR-0063): a syntax or type error names file, line and column; a partly broken layer applies what reads; `Project::Diagnostics()` returns them and `Load` logs each once.
- **The local file is the per-developer override** (ADR-0070): any config's fields, an asset root elsewhere, debug flags, the device index; above platform, below the command line; never committed and never written by the engine. `.gitignore` gains `*.project.local.json5`, and `README.md` gains a "Project file" subsection under "Building and testing", after the options table, naming the stack, the ignore line and `--set`.

### The demo scaffold

- **`DemoRunOptions` gains `project` and `overrides`**, read by `DemoRunOptions::FromCommandLine(argc, argv, problems)` (`--project <path>`, `--set Type.field=value`, repeatable; an unknown argument is a problem logged once) and merged over `FromEnvironment`; each demo's `main(argc, argv)` passes them through `DemoConfig::run`. `FENRIR_DEMO_*` stay the smoke-run switches (demo scaffold, not project layers) and apply over the project: a smoke run still hides the window, turns vsync off and validation on.
- **`DemoScaffold::Install` calls `Project::Load` once** and fills every config through `project.Get<T>(defaults)` with the values it builds today as `defaults`; a demo without `--project` runs on code defaults exactly as now. A `--project` naming a missing file is an error and `Run` returns non-zero. `fenrir_add_demo_app` copies an app's `<App>.project.json5`, where it has one, beside the executable.

### Import sidecars

- **`<source>.import.json5` beside a model or image** (`hero.glb.import.json5`, `wall.png.import.json5`): the envelope of a described options type, options only, no identity (ADR-0062), nothing required, defaults when missing (ADR-0070). Read by the store at load through the `AssetSource` seam ("does this path exist", "the bytes at this path") before the importer runs; a sidecar that fails to read applies the fields that parsed, defaults the rest, and makes the asset's `LoadResult` `Partial` with the located diagnostics. An image embedded in a model has no sidecar and takes defaults.
- **`Fenrir::ModelImportOptions`** (`settings` role, `@version 1`, FenrirAssets): `scale` (1.0, multiplied into the root nodes' transforms, where ufbx's unit conversion already sits), `axes` (`ImportAxes { FromFile, YUp, ZUp }`: the up axis the file's data is in when its header is absent or wrong, OBJ having none), `keep { meshes, materials, images, skins, clips }` (all true; a dropped kind leaves its labels unloaded, a mesh with no material or a material with no image taking the store's white defaults), `clips` (a list of clip names to keep, empty for all) and `skeletonRoot` (a node name overriding `skin.skeleton` and the nearest-non-bone rule). `ImportModel(file, options)` takes it.
- **`Fenrir::ImageImportOptions`** (`settings` role, `@version 1`, FenrirAssets): `colourSpace` (`ImportColourSpace { FromUse, Srgb, Linear }`, `FromUse` being today's rule) and `mipmaps` (true). `ImportImage(bytes, options)` takes it; `ImageData` gains `mipmaps`, which the renderer's `ImageUpload` honours, uploading one level when false. Sampler defaults stay on the material.
- **The sidecar is watched beside its source** (ADR-0067): the store keeps a `ReloadWatcher` on the sidecar path of every imported model and image whether or not the file exists, so creating, editing or deleting a sidecar reimports the asset through its importer and `Replace`, bumping the revision and listing it in `Reloaded()`.
- **No import cache**: imports run at load; the cache stays in the map's fog with its graduation condition.

### Not assets

Project and import files are described types in the one envelope with the format version, type versions and upgraders of ADR-0063, but they are not assets: no `AssetHandle`, no owner, no store slot, no hot reload. `Project` is a plain copyable value; a config change is a restart, and an import option change reaches the running app only through the asset it configures. Neither is written by the engine, so neither reaches the writer or the move tool. A cooked build has no sidecars (imports are baked, ADR-0071) and the local layer is never cooked; the project file's cooked twin is the cooking effort's.

### Testing

- **`Project` headless, no `App`**: fixture stacks under `packages/FenrirAssets/Tests/fixtures/project/` (a project file, one per platform, each a partial envelope), copied to a temporary directory where the test writes the local file (it cannot be committed under its own ignore line) and passes `--set` strings; checks field-by-field merge, list replacement, each field's provenance, path resolution against the layer's directory, an unknown field's warning through the `RecordingLogger` of `AssetsTest.hpp`, an unknown type kept opaque, a missing platform file ignored, a missing project file's one diagnostic, a lower-versioned tree's warning, an upgrader registered on the `Project` raising a test-local settings type described through the real generator (the data-assets spec's test `.api.toml`), and the logged stack. `Get<AssetsConfig>` is the engine type under test here; FenrirWindow, FenrirRenderer, FenrirPhysics and FenrirScripting each add one `Get<T>` round trip over a fixture file to their own suite.
- **Sidecars**: prior art is `packages/FenrirAssets/Tests/Models.cpp` and `Images.cpp` over `fixtures/models/scene.{gltf,glb,fbx,obj}`, `units.fbx`, `skinned.gltf`, `fourbytwo.png` and `twobytwo.hdr` on the headless `App`. A test copies a model or image to a temporary root with and without a sidecar and checks each option's effect: a root scaled, OBJ read Z-up, a dropped kind's label failing with a diagnostic, a kept clip list, the skeleton root moved, a PNG forced linear, `mipmaps` false on the image; a broken sidecar gives `Partial` with a located diagnostic and the defaults.
- **The watcher**: the data-assets spec's fake-clock harness; a sidecar written beside an already loaded model between frames reimports it, the handle in `Reloaded()` and the new scale on the root.
- **The scaffold**: `examples/DemoScaffold/Tests/` gains `FromCommandLine` cases (each switch, repeats, an unknown argument's problem) and a null-backend smoke run through `--project` over a fixture that sets `msaa` and the window title. No window.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | The six configs in the `settings` role: selections, `@transient`, milliseconds as `i64`, the `.feproj` comment corrected | — |
| 02 | `Fenrir::Project`: the envelope, `Load` of one file, `Get<T>` over code defaults, `ProjectConfig`, unknown fields and types, upgraders | 01 |
| 03 | The five layers: platform, local and `--set`, field-by-field merge, provenance and the start-up log; `.gitignore` and the README section | 02 |
| 04 | `--project` and `--set` in the demo scaffold: `FromCommandLine`, `main(argc, argv)`, `Install` through `Get<T>`, code defaults without a file | 03 |
| 05 | `ModelImportOptions` and the model sidecar: scale, axes, keep, clips, skeleton root, read at load, defaults when missing | 01 |
| 06 | `ImageImportOptions` and the image sidecar: colour space, `mipmaps` through `ImageData` to `ImageUpload` | 05 |
| 07 | Sidecars watched beside their source: create, edit and delete reimport through the watcher | 06 |
| 08 | RendererDemo ships `RendererDemo.project.json5` beside its executable, loaded by its smoke runs | 04, 07 |

Ticket 01 touches six packages' headers, `.api.toml` files and generated descriptions and registrations, so it goes first alone. Tickets 02, 03, 05, 06 and 07 all touch `packages/FenrirAssets/src/`, its CMake and its tests, so they stack: 01, 02, 03, 05, 06, 07; 06 also touches `packages/FenrirRenderer/src/` for `ImageUpload`. Ticket 04 touches only `examples/DemoScaffold/` and each demo's `main.cpp`, cut from 03's branch; 08 touches RendererDemo and the scaffold's CMake, after 04 and 07.

## Out of scope

Everything the other five specs own: the description, roles, kinds and `Describe<T>` (types spec); the tree, reader, writer, upgraders and diagnostics (format spec); the envelope reader, `AssetSource`, the watcher, `Replace` and `Reloaded()` (data-assets spec); scene `resources` and the per-scene environment (scenes spec); any C# view of the project or options (C# spec: the project is read before scripting starts, and the configs never cross). From the map's fog and out-of-scope lists: the import cache; the cooked twin of the project file and the baking of imported results (Fenrir cooking); editor UI for settings; the demo apps' scene lists, which stay code. Also: environment variables as a layer, settings as data assets in the store, an input map or packaging settings (no package reads one), sampler defaults at import, a `duration` kind, and writing any project or import file from the engine.

## Gaps settled here

- `Get<T>(T defaults = T{})` takes the caller's code defaults as the bottom layer, so an app's own defaults (`DemoConfig::msaa`) sit below the files without a second mechanism; `Provenance` reports them as `Default`.
- A list in a higher layer replaces the whole list below it (`PhysicsConfig::collisionLayers`); only nested structs merge by field.
- `ScriptingConfig`'s `std::chrono::milliseconds` fields are described as `i64` milliseconds rather than adding a `duration` kind.
- `Fenrir::ProjectConfig { name, firstScene }` is where ADR-0070's project name and first scene live, since no package's config holds them; `firstScene` is a plain asset path string, not a `reference`, so loading it is the game's call.
- With a project file present, an empty `AssetsConfig::root` means `assets/` beside the project file (ADR-0070 puts the file "beside the asset root"); a `--set` path resolves against the working directory, a layer file's path against that file's directory.
- Settings types start at `@version 1` with no upgraders; a lower-versioned tree reads with one warning, and upgraders, when a settings type first moves, are registered on the `Project` by the caller before `Get<T>`, since `Describe<T>` is tree-free and `Get<T>` runs before any registry exists.
- A sidecar's watcher exists whether or not the sidecar does, so creating one reimports the asset; a broken sidecar makes the asset's load `Partial` rather than failing it; an embedded image has no sidecar.
- `ImageImportOptions::mipmaps` needs `ImageData::mipmaps` for the renderer to read, the one change outside FenrirAssets and the demo scaffold; ticket 08's demo project file lives beside the executable, copied by `fenrir_add_demo_app`.
