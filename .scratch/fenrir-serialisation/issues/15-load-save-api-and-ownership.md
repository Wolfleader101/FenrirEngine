# Who loads and saves files, and what is the API?

Type: grilling
Status: open
Blocked by: 10, 13

## Question

Which package owns reading and writing scene files and data assets, and what does game code call?

- **Package:** FenrirAssets (a scene file and a data asset are assets loaded from the asset root, owned like any other), FenrirScene (a scene knows how to save itself), a new serialisation package both depend on, or a split (the format and registry low, the scene and asset loaders above). What each package may depend on, given FenrirAssets knows no ECS today and FenrirScene knows no files.
- **Loading a scene:** into a new scene, into the active scene additively, or either; whether it is synchronous on the main thread (as asset loads are, ADR-0021) and what async would need; the asset owner of what a scene file loads; the structural change it makes and when systems see it.
- **Saving:** a scene saved from a running app (the dev loop), what is written for entities made in code, and whether saving is engine or tool work.
- **Data assets:** `Assets.Load<T>` returning an `AssetHandle<T>` and a `Get` to the instance, or another door; adding one from code; replacing one (revision bump, as materials do).
- **Diagnostics:** what a failed or partial load reports and through what.
- **Tests:** how load and save are tested without a window (round-trip tests, golden files).

Consult `codebase-design` for the package shape and seams. Zoom into [What is a data asset?](10-what-a-data-asset-is.md) and [What does a scene file hold?](13-what-a-scene-file-holds.md); the assets findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md) give the as-is.
