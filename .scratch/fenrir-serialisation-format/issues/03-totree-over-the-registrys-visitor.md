# 03: `ToTree` over the registry's visitor: declaration order, defaults omitted, every kind's tree form

**What to build:** the first half of the bridge (ADR-0079): a described value becomes a value tree without its C++ type in hand, through FenrirTypes' one visitor, so a scene, a data asset or a config writes as the goldens show. Reading back is ticket 04, and references and entities are ticket 05.

- **`ToTree(const void* value, const TypeDescription&, const TypeRegistry&, const Resolvers&, const Value* prior = nullptr) -> Value`** (the spec, "The bridge"). It writes fields in declaration order and omits every field equal to its default, plus every disengaged `optional` whose default is disengaged too; one whose default holds a value is written `null`. The default is the same field of an instance the description's `Construct` made, visited beside the value; a nested struct compares field by field. The forms, kind by kind:
  - a struct is an `Object`, a `list` an `Array`, and an enum its enumerator name;
  - `string`, `fixed-string` and `path` are each a `String`;
  - a number is a `Number` of its own width;
  - a bound type is an `Array` of its components in visit order (`[0, 1.8, 0]`; a `Quat` as `x, y, z, w` once ticket 10 has landed);
  - a tagged union is an `Object` with `kind` (the tag's enumerator name) first, then the fields of `LiveAlternative` alone.

  `Resolvers` is declared here, but its callbacks are ticket 05's: a `reference` or `entity` field writes `null` in this ticket. `prior` is accepted and ignored until ticket 07.
- **The goldens through the bridge:** the third test from the spec. A hand-registered `MyGame::Sword`, its `MyGame::ParticleEffect` sub-asset, and test components named as the scene's (`Fenrir::Name`, `Fenrir::Transform`, `Fenrir::MeshRenderer`, `MyGame::BoneAttachment`, and `Fenrir::DirectionalLight` with the sample's `color` field) go through `ToTree` under an envelope the test builds by hand, and write the goldens byte for byte. The registry is test-only (ADR-0079 §7). Hand-written descriptions sit beside the test, as FenrirTypes' own tests do.

**Blocked by:** 02, 10.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/`, `src/`, `Tests/`, `Tests/CMakeLists.txt`)

**Status:** resolved

- [x] Every kind writes its form in one test per kind, the tagged union and the bound type included, with a field at its default omitted and one off its default written.
- [x] A disengaged `optional` is omitted, or written `null` when its default holds a value. An engaged one is written as its value.
- [x] The hand-registered sword and scene types write the two goldens byte for byte.
- [x] A struct registered with FenrirMath's real `Quat` writes `rotation` as `x, y, z, w`.
