---
status: accepted
---

# A project file layers each package's config by type name, and import sidecars hold options only

Six packages take an `Install(App&, Config)` struct (window, renderer, physics, animation, assets, scripting), each filled in code; imports run at load with no options and no cache; a `.feproj` exists only in a doc comment. The research found every settings store is a shallow tree of scalars, strings and asset references in the shape of Fenrir's config structs; that engines keep host state (per developer) and code wiring out of such files; that layering runs from code defaults through project, platform and user files to the command line, with Godot's filename suffix the cheapest platform rule and Unreal's twelve files the hardest to reason about; that a sidecar is committed, moved with its file, yields defaults when missing, and never carries identity where the path is the identity (ADR-0076); that sampler defaults sit on the material in Godot and Flax, as in Fenrir; and that no shipped game parses the editor's text file as-is, though three keep the shipped form a mechanical conversion of it.

We decided that **one project file in the envelope holds each package's config by canonical type name, layered over code defaults by platform, developer and command line, and that import sidecars are described options types beside their source**:

1. **One project file.** `<name>.project.json5` at the project root, beside the asset root, `type: "Fenrir::Project"`, its `data` a map from canonical type name to config, the shape of a scene's `resources`. Config structs gain a `settings` role in the description, read through the generated per-type visitor so paths and strings are allowed; code-only wiring (`ScriptingConfig::extensions`) is marked `@transient`. The `.feproj` doc comment is corrected.
2. **Install signatures stay; `Project::Get<T>()` fills a config before any Install.** `Fenrir::Project::Load(path)` in `FenrirAssets` reads the layered stack into a tree; `Get<T>()` fills the struct through `T`'s generated visitor resolved at compile time, needing no registry and no Install. An unknown field warns and drops; an unknown type is kept opaque (ADR-0077).
3. **Project versus scene.** Project: window, backend and quality, physics limits and defaults, asset root relative to the project file, script assembly, project name, first scene as an asset path, references to data assets rather than inlined values. Scene resources: per-scene environment. Device choice is never in the project file.
4. **Five layers, field by field, with provenance.** Code defaults → `<name>.project.json5` → `<name>.project.<platform>.json5` → `<name>.project.local.json5` → the command line (`--set Fenrir::RendererConfig.vsync=false`). Each file is a partial envelope. `Project` records which layer set each field and logs the stack once at start in development. No environment variables.
5. **The local file is the per-developer override.** Any config's fields, an asset root elsewhere, debug flags, the device index; above platform, below the command line; never committed, with the README giving the ignore line and the engine never writing it.
6. **Import sidecars hold options only.** `hero.glb.import.json5`, `wall.png.import.json5`: the envelope of an importer's described options type (`ModelImportOptions`: scale, axis conversion, sub-assets to keep, skeleton root; `ImageImportOptions`: colour space, mipmaps), no identity, nothing required, defaults when missing. Sampler defaults stay on the material. Imports run at load with no cache; a cooked build bakes imported results so no sidecar ships.
7. **Same format and registry, not data assets.** Project and import files are described types in the one envelope with versions and upgraders, but have no handle, owner or hot reload: a config change is a restart, an import option change reloads that asset through the watcher that already watches the source. Tests load fixture stacks headless and a model with and without its sidecar.

## Considered options

- **A plain config format with its own reader** (Godot's `ConfigFile`). A second grammar and reader for data that is config structs already; the envelope and visitor cost nothing more.
- **Settings as data assets in the store** (Flax). Handles and owners for values needed before the store exists, and a root that must load first; a config is read once and handed to Install.
- **Registering config types at Install and reading the project through the registry.** The registry is filled by the very Installs the configs feed; compile-time resolution of `Get<T>` avoids the cycle without a two-phase start.
- **Unreal's many files and environment variables.** More capable, and the one whose docs ship a command to find where a value came from; four files and a flag with provenance cover the cases found.
- **Identity in the sidecar** (Unity). Decided against by ADR-0076.
- **An import cache now.** Imports run at load today and nothing has shown the cost; the cache waits in the fog with a graduation condition.

## Consequences

- `tools/describe-api` gains the `settings` role and a `path` kind; each config struct joins its package's selection; `FenrirAssets` gains `Project` and the two import options types; importers read their sidecar.
- Demos take `--project <path>`; a game without a project file runs on code defaults as today.
- The cooked-form ticket decides the shipped twin of the project file and confirms that imported results are baked.
