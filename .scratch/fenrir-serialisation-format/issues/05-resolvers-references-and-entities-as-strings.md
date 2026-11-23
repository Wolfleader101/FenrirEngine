# 05: `Resolvers`: `reference` and `entity` fields as strings, `null` for none, `path` fields

**What to build:** the bridge hands asset references and entity references to its caller as strings, and never learns what an asset path or an entity id means (ADR-0076 §2, ADR-0078 §3). FenrirAssets and the scenes spec supply the meaning later. A `path` field is plain text.

- **`Resolvers`** (the spec, "The bridge") holds four optional callbacks over FenrirTypes' value types:
  - `readReference(path, const FieldInfo&) -> std::optional<ReferenceValue>` and `writeReference(ReferenceValue, const FieldInfo&) -> std::string`, for `reference` fields. The field's `type.target` names the target type.
  - `readEntity(id, const FieldInfo&) -> std::optional<EntityValue>` and `writeEntity(EntityValue, const FieldInfo&) -> std::string`, for `entity` fields.

  `ToTree` and `FromTree` call them and write what they return through the field's `ValueRef` (`SetReference`, `SetEntity`).
- **Empty and missing:**
  - An empty string from a write callback is written as `null`.
  - `null` reads as the kind's empty value (the empty handle, the null entity) with no diagnostic.
  - A read callback returning nothing leaves the field empty, counted in `unresolved`, with one diagnostic.
  - A missing callback leaves the field empty on read and writes `null`, counted in `unresolved`.
- **`path`:** a `String` both ways, with no callback. This ticket asserts nothing about its form beyond the text round-tripping; the writer's check for absolute or escaping paths is the data-assets spec's.

**Blocked by:** 04.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/`, `src/`, `Tests/`, `Tests/CMakeLists.txt`)

**Status:** resolved

- [x] Recording lambdas observe each callback once per field, with the field's name and target type, in both directions.
- [x] An empty result writes `null`, and `null` reads as the empty handle or the null entity with no diagnostic.
- [x] A missing callback or a failed read counts in `unresolved` and leaves the field empty.
- [x] A `path` field round-trips its text, non-ASCII UTF-8 included.
