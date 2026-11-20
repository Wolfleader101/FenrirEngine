# What dev-time data does the engine hold, and in what files?

Type: grilling
Status: resolved
Blocked by: 07, 12

## Question

What does the development workflow need in files that is not game content, and how does each relate to the data asset format?

- **Project settings:** whether the `Install(App&, Config)` configurations (renderer, window, physics, assets, scripting) can come from a project file, which settings belong there versus in a scene's resources, how they are layered (engine defaults, project, platform, per-developer), and whether the shipped game reads the same file or a cooked one.
- **Import sidecars:** a file beside a source model or image holding import options (scale, axes, which sub-assets to keep, texture settings, sampler defaults); whether it also carries the asset's stable identity (as [How are assets identified?](11-asset-identity-and-references.md) decided), what happens when it is missing, and where imported results are cached if at all (today imports run at load).
- **Per-developer overrides:** a local file ignored by version control for asset roots, debug flags and device choice; its name and precedence.
- **Same format, same registry:** whether these are data assets in all but role (described types, the one text format) or plain configuration with their own readers.

Zoom into [How do engines hold dev-time data?](07-dev-time-data-in-engines.md) and [Which text format do files use?](12-which-text-format-and-versioning.md).

## Answer

ADR: `docs/adr/0084-a-project-file-layers-each-packages-config-by-type-name-and-import-sidecars-hold-options-only.md`. Glossary: Project file, Import sidecar (and Asset root widened). One grilling round, every recommendation accepted.

- **Project settings:** one `<name>.project.json5` at the project root, `type: "Fenrir::Project"`, its `data` a map from canonical type name to each package's Install config, the configs gaining a `settings` role read through the generated visitor (paths and strings allowed, `ScriptingConfig::extensions` marked `@transient`); `Fenrir::Project::Load(path)` in `FenrirAssets` reads the stack and `project.Get<T>()` fills a config through `T`'s visitor at compile time, before any Install, so Install signatures stay; a demo takes `--project <path>`. Project holds window, backend and quality, physics limits, asset root relative to the project file, script assembly, project name, first scene as an asset path and references to data assets; scene resources hold per-scene environment; device choice is never committed. Five layers merged field by field with provenance logged once at start: code defaults, project, `<name>.project.<platform>.json5`, `<name>.project.local.json5`, command line `--set Type.field=value`; no environment variables. The `.feproj` doc comment is corrected.
- **Import sidecars:** `<source>.import.json5` beside a model or image, the envelope of a described options type (`ModelImportOptions`, `ImageImportOptions`), options only, no identity, defaults when missing; sampler defaults stay on the material; imports still run at load, with **Import cache** in the fog, and a cooked build bakes imported results so no sidecar ships.
- **Per-developer overrides:** the `.local.` project file, above platform and below the command line, never committed, the README giving the ignore line.
- **Same format, same registry:** yes, described types in the one envelope with versions and upgraders, but not data assets: no handle, owner or hot reload (a config change is a restart; an import option change reloads through the watcher on the source). Tests load fixture stacks headless and a model with and without its sidecar.
- **Left to other tickets:** the shipped twin of the project file and the baking of imported results (20).
