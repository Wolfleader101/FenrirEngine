# 09: The six configs join their selections under `settings`, `ScriptingConfig::extensions` is `@transient`, and `Describe<T>` exists for each

**What to build:** `WindowConfig`, `RendererConfig`, `PhysicsConfig`, `AnimationConfig`, `AssetsConfig` and `ScriptingConfig` are described types in the `settings` role, each with a generated visitor reachable through `Describe<T>` at compile time, so the dev-time spec's `Project::Get<T>()` can fill one from a file before any `Install` (ADR-0084). The code wiring in `ScriptingConfig::extensions` is marked `@transient` and never described as a field; the `.feproj` doc comment on `scriptAssembly` is corrected to the project file. No file is read in this ticket: the configs' behaviour in `Install` is unchanged.

- **Selections:** each of the six packages' `.api.toml` gains its config under a `[settings]` section (or the role key the selection format uses), with `@version 1` in the header's doc comment and `@formerly` where a field was renamed in history (none known). `WindowSize`, `CollisionLayer` and `Math::Vec3` describe as nested values where a config holds them; `std::chrono::milliseconds` fields in `ScriptingConfig` are described as `i64` milliseconds through an alias the tool records (the dev-time spec's gap), or the three fields change type to `std::int64_t` milliseconds if the tool cannot see through `std::chrono::duration`, whichever the implementer finds smaller, with the doc comment saying the unit either way.
- **`@transient`:** `ScriptingConfig::extensions` carries `@transient` and is left out of the description's fields; the tool already accepts the command (ticket 02) and this is its first use on a field, so the tool's rule that a field-level `@transient` drops the field from the description is confirmed or added here.
- **Paths and strings:** `AssetsConfig::root` and `ScriptingConfig`'s four paths describe as `path`; `WindowConfig::title` and `CollisionLayer::name` as `string`; `PhysicsConfig::collisionLayers` as a `list` of nested `CollisionLayer`.
- **Doc comment:** `ScriptingConfig::scriptAssembly`'s comment names `Fenrir::ScriptingConfig.scriptAssembly` in `<name>.project.json5` rather than the `.feproj`.
- **Regeneration:** each package's description and registration regenerate with the new type; its generated header gains `Describe<XConfig>`; the per-package defaults test covers the config.

**Blocked by:** 07.

**Touches:** `packages/FenrirWindow/` (`include/FenrirWindow/WindowTypes.hpp`, `api/`), `packages/FenrirRenderer/` (`include/FenrirRenderer/Renderer.hpp`, `api/`), `packages/FenrirPhysics/` (`include/FenrirPhysics/Physics.hpp`, `api/`), `packages/FenrirAnimation/` (`include/FenrirAnimation/Animation.hpp`, `api/`), `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `api/`), `packages/FenrirScripting/` (`include/FenrirScripting/Scripting.hpp`, `api/`), each package's generated `Types.hpp` and `Types.cpp`, `tools/describe-api/src/` (only if the field-level `@transient` rule or the duration alias needs adding)

**Status:** ready-for-agent

- [ ] Each of the six descriptions records its config with role `settings`, `version` 1, every field with the kind the header implies (`path`, `string`, `list`, nested, `i64` for durations), and no `extensions` field.
- [ ] `Describe<RendererConfig>()`, `Describe<PhysicsConfig>()` and `Describe<AssetsConfig>()` yield descriptions whose visitor over a default-constructed config reads the header's defaults, in a test that constructs no `App`.
- [ ] A recording visitor over a `PhysicsConfig` with two collision layers sees the `list` with two nested `CollisionLayer` values and their `string` names.
- [ ] `fenrir-describe-api`, `fenrir-generate-bindings` and `fenrir-generate-type-registry` regenerate byte-identical; Iris.Bindings skips the six types.
- [ ] The `.feproj` no longer appears anywhere in the tree.
