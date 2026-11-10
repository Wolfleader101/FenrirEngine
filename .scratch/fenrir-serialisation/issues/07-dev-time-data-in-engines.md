# How do engines hold dev-time data: project settings, import sidecars and per-developer overrides?

Type: research
Status: open
Blocked by: none

## Question

How do engines store the data the development workflow needs that is not game content, in the areas that constrain Fenrir's dev-time data: Godot's `project.godot`, `override.cfg` and `.import` sidecars with `.godot/imported/`; Unity's `ProjectSettings/`, `.meta` files with importer settings and GUIDs, `UserSettings/` and `Library/`; Unreal's `Config/*.ini` hierarchy (Base, Default, platform, Saved, per-user) and import options stored on the asset; Bevy's and Flax's equivalents.

- **Project settings:** what is a project setting versus a scene's or an asset's (window, rendering options, physics defaults, input bindings, package configuration such as what Fenrir's `Install(App&, Config)` takes), how settings are layered (defaults, project, platform, user), and how the shipped game reads them.
- **Import sidecars:** what sits beside a source asset (scale and axes for a model, which sub-assets to keep, texture compression, sampler defaults), whether the sidecar is the asset's identity (Unity's `.meta` GUID) or only its options (Godot), what happens when the sidecar is missing or the source moves, and where imported results are cached.
- **Per-developer overrides:** local window positions and last-opened scene aside (editor state, out of scope), what dev-only overrides exist (local asset roots, debug flags, device selection), where they live, and how they stay out of version control.
- **Shipping:** which of these files ship, which are baked into the executable or a pack, and which exist only in development.

Write findings to a Markdown file on the `research/dev-time-data` branch (`.scratch/fenrir-serialisation/research/dev-time-data.md`), citing each claim's source and pinning the versions read, and link it here.
