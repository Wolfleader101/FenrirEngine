# How are assets identified, and how do files reference them?

Type: grilling
Status: open
Blocked by: 01, 02, 10

## Question

How does a file name an asset or data asset so the reference survives editing, moving files and cooking? **ADR.**

- **Identity:** the path relative to the asset root (as `Assets` loads today, with `#label` for sub-assets), a stable id assigned at import (Unity's GUID, Godot's UID), or both (a path for people, an id for the engine). Where an id would live (a sidecar, the file's own header, an index), who assigns it, and what happens to an id on copy, rename or move.
- **Reference kinds:** a component's `AssetHandle<Mesh>` written to a scene file; a data asset's field naming another data asset; a scene naming a prefab source; a reference into a sub-asset. Whether references are typed in the file, and how a reference to a missing or wrong-typed target is reported and shown.
- **Resolution:** when a referenced asset is loaded (on scene load, lazily on first use, by the game), with which owner (ADR-0021), and how a stale handle is told apart from an unloaded one.
- **Cooking:** what the identity must guarantee so a cooked form can replace paths with offsets without touching game code.

Zoom into [How do engines type, inherit and reference data assets?](01-how-engines-type-data-assets.md) and [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md); the data asset's shape is [What is a data asset?](10-what-a-data-asset-is.md)'s.
