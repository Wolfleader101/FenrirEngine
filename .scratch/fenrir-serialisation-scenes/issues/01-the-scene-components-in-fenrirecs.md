# 01: `EntityId`, `Transient`, `UnknownComponents` and `Instance` in FenrirECS, with read-back state `@transient`

**What to build:** the four components a scene file needs, and no `Entity` left in read-back state for a file to write (ADR-0078, ADR-0080; the spec's "The components").

- **`EntityId`:** a described `component` holding one `u64`, where zero is "no id".
  - Add a helper that mints a five-character id: 25 random bits, spelled in lower-case Crockford base32 without padding.
  - Add helpers that spell an id as that string and parse one back. A longer id parses and spells back unchanged.
- **`Transient`:** an empty, undescribed tag struct, never registered. It is reached through the typed `HasComponent`, `AddComponent` and `RemoveComponent`.
  - `ComponentType::Native` keeps refusing empty types.
  - If the typed `AddComponent` cannot take an empty type, because it returns `T&` from EnTT's `emplace`, give it a path for empty tags that returns nothing. Change nothing else.
- **`UnknownComponents`:** an undescribed component holding, per opaque component:
  - its canonical type name;
  - its type version;
  - its value's subtree, as `EncodeTree` bytes.

  FenrirECS holds the bytes and never decodes them, so it links nothing new.
- **`Instance { source }`:** a described `component`, where `source` is a `fixed-string` asset path.
- **Read-back state:** mark `CharacterBody.state` and `VehicleWheel.state` `@transient`.
- **Regenerate:** commit the regenerated descriptions, bindings and registrations of FenrirECS and FenrirPhysics.

**Blocked by:** none.

**Touches:** `packages/FenrirECS/` (`include/FenrirECS/` new headers, `src/`, its `.api.toml`, `src/Generated/`, `Tests/`), `packages/FenrirPhysics/include/FenrirPhysics/` (`Characters.hpp`, the vehicle header), `packages/FenrirPhysics/src/Generated/`, the generated C# bindings for both

**Status:** ready-for-agent

- [ ] A minted `EntityId` is five lower-case Crockford characters, and a parsed longer id spells back unchanged.
- [ ] `Transient` is added, tested and removed through the typed API, and is in no registration.
- [ ] `UnknownComponents` holds an opaque value's bytes and gives them back unchanged.
- [ ] `EntityId` and `Instance` are found through `ComponentType::ForName`.
- [ ] `CharacterBody.state` and `VehicleWheel.state` are `@transient`, and every generated output regenerates clean.
