# 10: describe-api: a bound type's selection names its components' order, so `Quat` visits and writes as `x, y, z, w`

**What to build:** a bound type's components visit in an order its selection may name, not only in storage order. FenrirMath's `Quat` is glm's and is stored `w` first, so it visits as `w, x, y, z` today, and files would hold `rotation: [w, x, y, z]`. The goldens, glTF and C#'s `Quaternion` all write `x, y, z, w`. The user chose this over storing the quaternion `x` first (`GLM_FORCE_QUAT_DATA_XYZW`), which would move the layout under every binding (the spec, "Gaps settled here").

- **The selection:** a `[components]` table in a `.api.toml` maps a selected bound type to its component names in the order files use: `"Fenrir::Math::Quat" = ["x", "y", "z", "w"]`. The tool fails, naming the type and the name, when:
  - a listed name is not one of the type's recorded components;
  - one is missing or listed twice;
  - the type is not a selected bound type.
- **The description:** describe-api records `components` in that order, each keeping its own offset. A bound type absent from the table keeps storage order. `SCHEMA.md` and Iris's copy say so. Iris.Bindings ignores `components` and its bindings do not move.
- **The registration:** `tools/generate-type-registry` already writes a bound type's fields from `components`, so FenrirMath's regenerated registration visits `Quat` as `x, y, z, w`. Check that its generated defaults test and every visitor test still pass.
- **Regeneration:** configure with `FENRIR_ENABLE_RENDERER=ON`, then run `fenrir-describe-api`, `fenrir-generate-bindings` and `fenrir-generate-type-registry`, and commit the results. CI's `api-descriptions` check fails on a dirty tree.

**Blocked by:** none.

**Touches:** `tools/describe-api/src/` (`Selection.cs`, `Describer.cs`), `tools/describe-api/SCHEMA.md`, `tools/describe-api/README.md`, `tools/describe-api/tests/` (fixture `.api.toml`, `Fixture.api.json`, `Failing` cases), `packages/Iris/docs/api-description.md`, `packages/FenrirMath/api/FenrirMath.api.toml`, the regenerated `FenrirMath.api.json` and `FenrirMath` registration

**Status:** ready-for-agent

- [ ] The fixture's bound type, given a `[components]` order, records its components in that order with unchanged offsets. Its `Fixture.api.json` is compared byte for byte.
- [ ] An unknown name, a missing or repeated one, and a type that is not a bound type each fail, naming the type and the name.
- [ ] FenrirMath's description and registration visit `Quat` as `x, y, z, w`, and its defaults test passes.
- [ ] Every binding regenerates byte-identical.
